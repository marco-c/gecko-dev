/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! High-level interface to CSS selector matching.

#![allow(unsafe_code)]
#![deny(missing_docs)]

use crate::context::{ElementCascadeInputs, QuirksMode, SelectorFlagsMap};
use crate::context::{SharedStyleContext, StyleContext};
use crate::data::ElementData;
use crate::dom::TElement;
use crate::invalidation::element::restyle_hints::RestyleHint;
use crate::properties::longhands::display::computed_value::T as Display;
use crate::properties::ComputedValues;
use crate::rule_tree::{CascadeLevel, StrongRuleNode};
use crate::selector_parser::{PseudoElement, RestyleDamage};
use crate::style_resolver::ResolvedElementStyles;
use crate::traversal_flags::TraversalFlags;
use selectors::matching::ElementSelectorFlags;
use servo_arc::{Arc, ArcBorrow};

/// Represents the result of comparing an element's old and new style.
#[derive(Debug)]
pub struct StyleDifference {
    /// The resulting damage.
    pub damage: RestyleDamage,

    /// Whether any styles changed.
    pub change: StyleChange,
}

/// Represents whether or not the style of an element has changed.
#[derive(Clone, Copy, Debug)]
pub enum StyleChange {
    /// The style hasn't changed.
    Unchanged,
    /// The style has changed.
    Changed {
        /// Whether only reset structs changed.
        reset_only: bool,
    },
}

/// Whether or not newly computed values for an element need to be cascade
/// to children.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum ChildCascadeRequirement {
    /// Old and new computed values were the same, or we otherwise know that
    /// we won't bother recomputing style for children, so we can skip cascading
    /// the new values into child elements.
    CanSkipCascade = 0,
    /// The same as `MustCascadeChildren`, but we only need to actually
    /// recascade if the child inherits any explicit reset style.
    MustCascadeChildrenIfInheritResetStyle = 1,
    /// Old and new computed values were different, so we must cascade the
    /// new values to children.
    MustCascadeChildren = 2,
    /// The same as `MustCascadeChildren`, but for the entire subtree.  This is
    /// used to handle root font-size updates needing to recascade the whole
    /// document.
    MustCascadeDescendants = 3,
}

impl ChildCascadeRequirement {
    /// Whether we can unconditionally skip the cascade.
    pub fn can_skip_cascade(&self) -> bool {
        matches!(*self, ChildCascadeRequirement::CanSkipCascade)
    }
}

/// Determines which styles are being cascaded currently.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum CascadeVisitedMode {
    /// Cascade the regular, unvisited styles.
    Unvisited,
    /// Cascade the styles used when an element's relevant link is visited.  A
    /// "relevant link" is the element being matched if it is a link or the
    /// nearest ancestor link.
    Visited,
}

trait PrivateMatchMethods: TElement {
    /// Updates the rule nodes without re-running selector matching, using just
    /// the rule tree, for a specific visited mode.
    ///
    /// Returns true if an !important rule was replaced.
    fn replace_rules_internal(
        &self,
        replacements: RestyleHint,
        context: &mut StyleContext<Self>,
        cascade_visited: CascadeVisitedMode,
        cascade_inputs: &mut ElementCascadeInputs,
    ) -> bool {
        use crate::properties::PropertyDeclarationBlock;
        use crate::shared_lock::Locked;

        debug_assert!(
            replacements.intersects(RestyleHint::replacements()) &&
                (replacements & !RestyleHint::replacements()).is_empty()
        );

        let stylist = &context.shared.stylist;
        let guards = &context.shared.guards;

        let primary_rules = match cascade_visited {
            CascadeVisitedMode::Unvisited => cascade_inputs.primary.rules.as_mut(),
            CascadeVisitedMode::Visited => cascade_inputs.primary.visited_rules.as_mut(),
        };

        let primary_rules = match primary_rules {
            Some(r) => r,
            None => return false,
        };

        let replace_rule_node = |level: CascadeLevel,
                                 pdb: Option<ArcBorrow<Locked<PropertyDeclarationBlock>>>,
                                 path: &mut StrongRuleNode|
         -> bool {
            let mut important_rules_changed = false;
            let new_node = stylist.rule_tree().update_rule_at_level(
                level,
                pdb,
                path,
                guards,
                &mut important_rules_changed,
            );
            if let Some(n) = new_node {
                *path = n;
            }
            important_rules_changed
        };

        if !context.shared.traversal_flags.for_animation_only() {
            let mut result = false;
            if replacements.contains(RestyleHint::RESTYLE_STYLE_ATTRIBUTE) {
                let style_attribute = self.style_attribute();
                result |= replace_rule_node(
                    CascadeLevel::StyleAttributeNormal,
                    style_attribute,
                    primary_rules,
                );
                result |= replace_rule_node(
                    CascadeLevel::StyleAttributeImportant,
                    style_attribute,
                    primary_rules,
                );
                // FIXME(emilio): Still a hack!
                self.unset_dirty_style_attribute();
            }
            return result;
        }

        // Animation restyle hints are processed prior to other restyle
        // hints in the animation-only traversal.
        //
        // Non-animation restyle hints will be processed in a subsequent
        // normal traversal.
        if replacements.intersects(RestyleHint::for_animations()) {
            debug_assert!(context.shared.traversal_flags.for_animation_only());

            if replacements.contains(RestyleHint::RESTYLE_SMIL) {
                replace_rule_node(
                    CascadeLevel::SMILOverride,
                    self.smil_override(),
                    primary_rules,
                );
            }

            if replacements.contains(RestyleHint::RESTYLE_CSS_TRANSITIONS) {
                replace_rule_node(
                    CascadeLevel::Transitions,
                    self.transition_rule().as_ref().map(|a| a.borrow_arc()),
                    primary_rules,
                );
            }

            if replacements.contains(RestyleHint::RESTYLE_CSS_ANIMATIONS) {
                replace_rule_node(
                    CascadeLevel::Animations,
                    self.animation_rule().as_ref().map(|a| a.borrow_arc()),
                    primary_rules,
                );
            }
        }

        false
    }

    /// If there is no transition rule in the ComputedValues, it returns None.
    #[cfg(feature = "gecko")]
    fn after_change_style(
        &self,
        context: &mut StyleContext<Self>,
        primary_style: &Arc<ComputedValues>,
    ) -> Option<Arc<ComputedValues>> {
        use crate::context::CascadeInputs;
        use crate::style_resolver::{PseudoElementResolution, StyleResolverForElement};
        use crate::stylist::RuleInclusion;

        let rule_node = primary_style.rules();
        let without_transition_rules = context
            .shared
            .stylist
            .rule_tree()
            .remove_transition_rule_if_applicable(rule_node);
        if without_transition_rules == *rule_node {
            // We don't have transition rule in this case, so return None to let
            // the caller use the original ComputedValues.
            return None;
        }

        // FIXME(bug 868975): We probably need to transition visited style as
        // well.
        let inputs = CascadeInputs {
            rules: Some(without_transition_rules),
            visited_rules: primary_style.visited_rules().cloned(),
        };

        // Actually `PseudoElementResolution` doesn't really matter.
        let style = StyleResolverForElement::new(
            *self,
            context,
            RuleInclusion::All,
            PseudoElementResolution::IfApplicable,
        )
        .cascade_style_and_visited_with_default_parents(inputs);

        Some(style.0)
    }

    #[cfg(feature = "gecko")]
    fn needs_animations_update(
        &self,
        context: &mut StyleContext<Self>,
        old_style: Option<&ComputedValues>,
        new_style: &ComputedValues,
    ) -> bool {
        let new_box_style = new_style.get_box();
        let new_style_specifies_animations = new_box_style.specifies_animations();

        let has_animations = self.has_css_animations();
        if !new_style_specifies_animations && !has_animations {
            return false;
        }

        let old_style = match old_style {
            Some(old) => old,
            // If we have no old style but have animations, we may be a
            // pseudo-element which was re-created without style changes.
            //
            // This can happen when we reframe the pseudo-element without
            // restyling it (due to content insertion on a flex container or
            // such, for example). See bug 1564366.
            //
            // FIXME(emilio): The really right fix for this is keeping the
            // pseudo-element itself around on reframes, but that's a bit
            // harder. If we do that we can probably remove quite a lot of the
            // EffectSet complexity though, since right now it's stored on the
            // parent element for pseudo-elements given we need to keep it
            // around...
            None => {
                return new_style_specifies_animations || new_style.is_pseudo_style();
            }
        };

        let old_box_style = old_style.get_box();

        let keyframes_could_have_changed = context
            .shared
            .traversal_flags
            .contains(TraversalFlags::ForCSSRuleChanges);

        // If the traversal is triggered due to changes in CSS rules changes, we
        // need to try to update all CSS animations on the element if the
        // element has or will have CSS animation style regardless of whether
        // the animation is running or not.
        //
        // TODO: We should check which @keyframes were added/changed/deleted and
        // update only animations corresponding to those @keyframes.
        if keyframes_could_have_changed {
            return true;
        }

        // If the animations changed, well...
        if !old_box_style.animations_equals(new_box_style) {
            return true;
        }

        let old_display = old_box_style.clone_display();
        let new_display = new_box_style.clone_display();

        // If we were display: none, we may need to trigger animations.
        if old_display == Display::None && new_display != Display::None {
            return new_style_specifies_animations;
        }

        // If we are becoming display: none, we may need to stop animations.
        if old_display != Display::None && new_display == Display::None {
            return has_animations;
        }

        // We might need to update animations if writing-mode or direction
        // changed, and any of the animations contained logical properties.
        //
        // We may want to be more granular, but it's probably not worth it.
        if new_style.writing_mode != old_style.writing_mode {
            return has_animations;
        }

        false
    }

    /// Create a SequentialTask for resolving descendants in a SMIL display
    /// property animation if the display property changed from none.
    #[cfg(feature = "gecko")]
    fn handle_display_change_for_smil_if_needed(
        &self,
        context: &mut StyleContext<Self>,
        old_values: Option<&ComputedValues>,
        new_values: &ComputedValues,
        restyle_hints: RestyleHint,
    ) {
        use crate::context::PostAnimationTasks;

        if !restyle_hints.intersects(RestyleHint::RESTYLE_SMIL) {
            return;
        }

        if new_values.is_display_property_changed_from_none(old_values) {
            // When display value is changed from none to other, we need to
            // traverse descendant elements in a subsequent normal
            // traversal (we can't traverse them in this animation-only restyle
            // since we have no way to know whether the decendants
            // need to be traversed at the beginning of the animation-only
            // restyle).
            let task = ::context::SequentialTask::process_post_animation(
                *self,
                PostAnimationTasks::DISPLAY_CHANGED_FROM_NONE_FOR_SMIL,
            );
            context.thread_local.tasks.push(task);
        }
    }

    #[cfg(feature = "gecko")]
    fn process_animations(
        &self,
        context: &mut StyleContext<Self>,
        old_values: &mut Option<Arc<ComputedValues>>,
        new_values: &mut Arc<ComputedValues>,
        restyle_hint: RestyleHint,
        important_rules_changed: bool,
    ) {
        use crate::context::UpdateAnimationsTasks;

        if context.shared.traversal_flags.for_animation_only() {
            self.handle_display_change_for_smil_if_needed(
                context,
                old_values.as_ref().map(|v| &**v),
                new_values,
                restyle_hint,
            );
            return;
        }

        // Bug 868975: These steps should examine and update the visited styles
        // in addition to the unvisited styles.

        let mut tasks = UpdateAnimationsTasks::empty();
        if self.needs_animations_update(context, old_values.as_ref().map(|s| &**s), new_values) {
            tasks.insert(UpdateAnimationsTasks::CSS_ANIMATIONS);
        }

        let before_change_style = if self
            .might_need_transitions_update(old_values.as_ref().map(|s| &**s), new_values)
        {
            let after_change_style = if self.has_css_transitions() {
                self.after_change_style(context, new_values)
            } else {
                None
            };

            // In order to avoid creating a SequentialTask for transitions which
            // may not be updated, we check it per property to make sure Gecko
            // side will really update transition.
            let needs_transitions_update = {
                // We borrow new_values here, so need to add a scope to make
                // sure we release it before assigning a new value to it.
                let after_change_style_ref = after_change_style.as_ref().unwrap_or(&new_values);

                self.needs_transitions_update(old_values.as_ref().unwrap(), after_change_style_ref)
            };

            if needs_transitions_update {
                if let Some(values_without_transitions) = after_change_style {
                    *new_values = values_without_transitions;
                }
                tasks.insert(UpdateAnimationsTasks::CSS_TRANSITIONS);

                // We need to clone old_values into SequentialTask, so we can
                // use it later.
                old_values.clone()
            } else {
                None
            }
        } else {
            None
        };

        if self.has_animations() {
            tasks.insert(UpdateAnimationsTasks::EFFECT_PROPERTIES);
            if important_rules_changed {
                tasks.insert(UpdateAnimationsTasks::CASCADE_RESULTS);
            }
            if new_values.is_display_property_changed_from_none(old_values.as_ref().map(|s| &**s)) {
                tasks.insert(UpdateAnimationsTasks::DISPLAY_CHANGED_FROM_NONE);
            }
        }

        if !tasks.is_empty() {
            let task =
                ::context::SequentialTask::update_animations(*self, before_change_style, tasks);
            context.thread_local.tasks.push(task);
        }
    }

    #[cfg(feature = "servo")]
    fn process_animations(
        &self,
        context: &mut StyleContext<Self>,
        old_values: &mut Option<Arc<ComputedValues>>,
        new_values: &mut Arc<ComputedValues>,
        _restyle_hint: RestyleHint,
        _important_rules_changed: bool,
    ) {
        use crate::animation;
        use crate::dom::TNode;

        let mut possibly_expired_animations = vec![];
        let shared_context = context.shared;
        if let Some(ref mut old) = *old_values {
            // FIXME(emilio, #20116): This makes no sense.
            self.update_animations_for_cascade(
                shared_context,
                old,
                &mut possibly_expired_animations,
                &context.thread_local.font_metrics_provider,
            );
        }

        let new_animations_sender = &context.thread_local.new_animations_sender;
        let this_opaque = self.as_node().opaque();
        // Trigger any present animations if necessary.
        animation::maybe_start_animations(
            *self,
            &shared_context,
            new_animations_sender,
            this_opaque,
            &new_values,
        );

        // Trigger transitions if necessary. This will reset `new_values` back
        // to its old value if it did trigger a transition.
        if let Some(ref values) = *old_values {
            animation::start_transitions_if_applicable(
                new_animations_sender,
                this_opaque,
                &values,
                new_values,
                &shared_context.timer,
                &possibly_expired_animations,
            );
        }
    }

    /// Computes and applies non-redundant damage.
    fn accumulate_damage_for(
        &self,
        shared_context: &SharedStyleContext,
        damage: &mut RestyleDamage,
        old_values: &ComputedValues,
        new_values: &ComputedValues,
        pseudo: Option<&PseudoElement>,
    ) -> ChildCascadeRequirement {
        debug!("accumulate_damage_for: {:?}", self);
        debug_assert!(!shared_context
            .traversal_flags
            .contains(TraversalFlags::FinalAnimationTraversal));

        let difference = self.compute_style_difference(old_values, new_values, pseudo);

        *damage |= difference.damage;

        debug!(" > style difference: {:?}", difference);

        // We need to cascade the children in order to ensure the correct
        // propagation of inherited computed value flags.
        if old_values.flags.maybe_inherited() != new_values.flags.maybe_inherited() {
            debug!(
                " > flags changed: {:?} != {:?}",
                old_values.flags, new_values.flags
            );
            return ChildCascadeRequirement::MustCascadeChildren;
        }

        match difference.change {
            StyleChange::Unchanged => return ChildCascadeRequirement::CanSkipCascade,
            StyleChange::Changed { reset_only } => {
                // If inherited properties changed, the best we can do is
                // cascade the children.
                if !reset_only {
                    return ChildCascadeRequirement::MustCascadeChildren;
                }
            },
        }

        let old_display = old_values.get_box().clone_display();
        let new_display = new_values.get_box().clone_display();

        if old_display != new_display {
            // If we used to be a display: none element, and no longer are, our
            // children need to be restyled because they're unstyled.
            if old_display == Display::None {
                return ChildCascadeRequirement::MustCascadeChildren;
            }
            // Blockification of children may depend on our display value,
            // so we need to actually do the recascade. We could potentially
            // do better, but it doesn't seem worth it.
            if old_display.is_item_container() != new_display.is_item_container() {
                return ChildCascadeRequirement::MustCascadeChildren;
            }
            // We may also need to blockify and un-blockify descendants if our
            // display goes from / to display: contents, since the "layout
            // parent style" changes.
            if old_display.is_contents() || new_display.is_contents() {
                return ChildCascadeRequirement::MustCascadeChildren;
            }
            // Line break suppression may also be affected if the display
            // type changes from ruby to non-ruby.
            #[cfg(feature = "gecko")]
            {
                if old_display.is_ruby_type() != new_display.is_ruby_type() {
                    return ChildCascadeRequirement::MustCascadeChildren;
                }
            }
        }

        // Children with justify-items: auto may depend on our
        // justify-items property value.
        //
        // Similarly, we could potentially do better, but this really
        // seems not common enough to care about.
        #[cfg(feature = "gecko")]
        {
            use crate::values::specified::align::AlignFlags;

            let old_justify_items = old_values.get_position().clone_justify_items();
            let new_justify_items = new_values.get_position().clone_justify_items();

            let was_legacy_justify_items =
                old_justify_items.computed.0.contains(AlignFlags::LEGACY);

            let is_legacy_justify_items = new_justify_items.computed.0.contains(AlignFlags::LEGACY);

            if is_legacy_justify_items != was_legacy_justify_items {
                return ChildCascadeRequirement::MustCascadeChildren;
            }

            if was_legacy_justify_items && old_justify_items.computed != new_justify_items.computed
            {
                return ChildCascadeRequirement::MustCascadeChildren;
            }
        }

        #[cfg(feature = "servo")]
        {
            // We may need to set or propagate the CAN_BE_FRAGMENTED bit
            // on our children.
            if old_values.is_multicol() != new_values.is_multicol() {
                return ChildCascadeRequirement::MustCascadeChildren;
            }
        }

        // We could prove that, if our children don't inherit reset
        // properties, we can stop the cascade.
        ChildCascadeRequirement::MustCascadeChildrenIfInheritResetStyle
    }

    // FIXME(emilio, #20116): It's not clear to me that the name of this method
    // represents anything of what it does.
    //
    // Also, this function gets the old style, for some reason I don't really
    // get, but the functions called (mainly update_style_for_animation) expects
    // the new style, wtf?
    #[cfg(feature = "servo")]
    fn update_animations_for_cascade(
        &self,
        context: &SharedStyleContext,
        style: &mut Arc<ComputedValues>,
        possibly_expired_animations: &mut Vec<crate::animation::PropertyAnimation>,
        font_metrics: &dyn crate::font_metrics::FontMetricsProvider,
    ) {
        use crate::animation::{self, Animation, AnimationUpdate};
        use crate::dom::TNode;

        // Finish any expired transitions.
        let this_opaque = self.as_node().opaque();
        animation::complete_expired_transitions(this_opaque, style, context);

        // Merge any running animations into the current style, and cancel them.
        let had_running_animations = context
            .running_animations
            .read()
            .get(&this_opaque)
            .is_some();
        if !had_running_animations {
            return;
        }

        let mut all_running_animations = context.running_animations.write();
        for mut running_animation in all_running_animations.get_mut(&this_opaque).unwrap() {
            if let Animation::Transition(_, _, ref frame) = *running_animation {
                possibly_expired_animations.push(frame.property_animation.clone());
                continue;
            }

            let update = animation::update_style_for_animation::<Self>(
                context,
                &mut running_animation,
                style,
                font_metrics,
            );

            match *running_animation {
                Animation::Transition(..) => unreachable!(),
                Animation::Keyframes(_, _, _, ref mut state) => match update {
                    AnimationUpdate::Regular => {},
                    AnimationUpdate::AnimationCanceled => {
                        state.expired = true;
                    },
                },
            }
        }
    }
}

impl<E: TElement> PrivateMatchMethods for E {}

/// The public API that elements expose for selector matching.
pub trait MatchMethods: TElement {
    /// Returns the closest parent element that doesn't have a display: contents
    /// style (and thus generates a box).
    ///
    /// This is needed to correctly handle blockification of flex and grid
    /// items.
    ///
    /// Returns itself if the element has no parent. In practice this doesn't
    /// happen because the root element is blockified per spec, but it could
    /// happen if we decide to not blockify for roots of disconnected subtrees,
    /// which is a kind of dubious behavior.
    fn layout_parent(&self) -> Self {
        let mut current = self.clone();
        loop {
            current = match current.traversal_parent() {
                Some(el) => el,
                None => return current,
            };

            let is_display_contents = current
                .borrow_data()
                .unwrap()
                .styles
                .primary()
                .is_display_contents();

            if !is_display_contents {
                return current;
            }
        }
    }

    /// Updates the styles with the new ones, diffs them, and stores the restyle
    /// damage.
    fn finish_restyle(
        &self,
        context: &mut StyleContext<Self>,
        data: &mut ElementData,
        mut new_styles: ResolvedElementStyles,
        important_rules_changed: bool,
    ) -> ChildCascadeRequirement {
        use std::cmp;

        self.process_animations(
            context,
            &mut data.styles.primary,
            &mut new_styles.primary.style.0,
            data.hint,
            important_rules_changed,
        );

        // First of all, update the styles.
        let old_styles = data.set_styles(new_styles);

        let new_primary_style = data.styles.primary.as_ref().unwrap();

        let mut cascade_requirement = ChildCascadeRequirement::CanSkipCascade;
        if self.is_root() && !self.is_in_native_anonymous_subtree() {
            let device = context.shared.stylist.device();
            let new_font_size = new_primary_style.get_font().clone_font_size();

            if old_styles
                .primary
                .as_ref()
                .map_or(true, |s| s.get_font().clone_font_size() != new_font_size)
            {
                debug_assert!(self.owner_doc_matches_for_testing(device));
                device.set_root_font_size(new_font_size.size());
                // If the root font-size changed since last time, and something
                // in the document did use rem units, ensure we recascade the
                // entire tree.
                if device.used_root_font_size() {
                    cascade_requirement = ChildCascadeRequirement::MustCascadeDescendants;
                }
            }
        }

        if context.shared.stylist.quirks_mode() == QuirksMode::Quirks {
            if self.is_html_document_body_element() {
                // NOTE(emilio): We _could_ handle dynamic changes to it if it
                // changes and before we reach our children the cascade stops,
                // but we don't track right now whether we use the document body
                // color, and nobody else handles that properly anyway.

                let device = context.shared.stylist.device();

                // Needed for the "inherit from body" quirk.
                let text_color = new_primary_style.get_inherited_text().clone_color();
                device.set_body_text_color(text_color);
            }
        }

        // Don't accumulate damage if we're in the final animation traversal.
        if context
            .shared
            .traversal_flags
            .contains(TraversalFlags::FinalAnimationTraversal)
        {
            return ChildCascadeRequirement::MustCascadeChildren;
        }

        // Also, don't do anything if there was no style.
        let old_primary_style = match old_styles.primary {
            Some(s) => s,
            None => return ChildCascadeRequirement::MustCascadeChildren,
        };

        cascade_requirement = cmp::max(
            cascade_requirement,
            self.accumulate_damage_for(
                context.shared,
                &mut data.damage,
                &old_primary_style,
                new_primary_style,
                None,
            ),
        );

        if data.styles.pseudos.is_empty() && old_styles.pseudos.is_empty() {
            // This is the common case; no need to examine pseudos here.
            return cascade_requirement;
        }

        let pseudo_styles = old_styles
            .pseudos
            .as_array()
            .iter()
            .zip(data.styles.pseudos.as_array().iter());

        for (i, (old, new)) in pseudo_styles.enumerate() {
            match (old, new) {
                (&Some(ref old), &Some(ref new)) => {
                    self.accumulate_damage_for(
                        context.shared,
                        &mut data.damage,
                        old,
                        new,
                        Some(&PseudoElement::from_eager_index(i)),
                    );
                },
                (&None, &None) => {},
                _ => {
                    // It's possible that we're switching from not having
                    // ::before/::after at all to having styles for them but not
                    // actually having a useful pseudo-element.  Check for that
                    // case.
                    let pseudo = PseudoElement::from_eager_index(i);
                    let new_pseudo_should_exist =
                        new.as_ref().map_or(false, |s| pseudo.should_exist(s));
                    let old_pseudo_should_exist =
                        old.as_ref().map_or(false, |s| pseudo.should_exist(s));
                    if new_pseudo_should_exist != old_pseudo_should_exist {
                        data.damage |= RestyleDamage::reconstruct();
                        return cascade_requirement;
                    }
                },
            }
        }

        cascade_requirement
    }

    /// Applies selector flags to an element, deferring mutations of the parent
    /// until after the traversal.
    ///
    /// TODO(emilio): This is somewhat inefficient, because it doesn't take
    /// advantage of us knowing that the traversal is sequential.
    fn apply_selector_flags(
        &self,
        map: &mut SelectorFlagsMap<Self>,
        element: &Self,
        flags: ElementSelectorFlags,
    ) {
        // Handle flags that apply to the element.
        let self_flags = flags.for_self();
        if !self_flags.is_empty() {
            if element == self {
                // If this is the element we're styling, we have exclusive
                // access to the element, and thus it's fine inserting them,
                // even from the worker.
                unsafe {
                    element.set_selector_flags(self_flags);
                }
            } else {
                // Otherwise, this element is an ancestor of the current element
                // we're styling, and thus multiple children could write to it
                // if we did from here.
                //
                // Instead, we can read them, and post them if necessary as a
                // sequential task in order for them to be processed later.
                if !element.has_selector_flags(self_flags) {
                    map.insert_flags(*element, self_flags);
                }
            }
        }

        // Handle flags that apply to the parent.
        let parent_flags = flags.for_parent();
        if !parent_flags.is_empty() {
            if let Some(p) = element.parent_element() {
                if !p.has_selector_flags(parent_flags) {
                    map.insert_flags(p, parent_flags);
                }
            }
        }
    }

    /// Updates the rule nodes without re-running selector matching, using just
    /// the rule tree.
    ///
    /// Returns true if an !important rule was replaced.
    fn replace_rules(
        &self,
        replacements: RestyleHint,
        context: &mut StyleContext<Self>,
        cascade_inputs: &mut ElementCascadeInputs,
    ) -> bool {
        let mut result = false;
        result |= self.replace_rules_internal(
            replacements,
            context,
            CascadeVisitedMode::Unvisited,
            cascade_inputs,
        );
        result |= self.replace_rules_internal(
            replacements,
            context,
            CascadeVisitedMode::Visited,
            cascade_inputs,
        );
        result
    }

    /// Given the old and new style of this element, and whether it's a
    /// pseudo-element, compute the restyle damage used to determine which
    /// kind of layout or painting operations we'll need.
    fn compute_style_difference(
        &self,
        old_values: &ComputedValues,
        new_values: &ComputedValues,
        pseudo: Option<&PseudoElement>,
    ) -> StyleDifference {
        debug_assert!(pseudo.map_or(true, |p| p.is_eager()));
        RestyleDamage::compute_style_difference(old_values, new_values)
    }
}

impl<E: TElement> MatchMethods for E {}

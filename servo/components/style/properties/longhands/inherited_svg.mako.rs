/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

<%namespace name="helpers" file="/helpers.mako.rs" />

// SVG 1.1 (Second Edition)
// https://www.w3.org/TR/SVG/
<% data.new_style_struct("InheritedSVG", inherited=True, gecko_name="SVG") %>

// Section 10 - Text

${helpers.single_keyword(
    "text-anchor",
    "start middle end",
    products="gecko",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG/text.html#TextAnchorProperty",
)}

// Section 11 - Painting: Filling, Stroking and Marker Symbols
${helpers.single_keyword(
    "color-interpolation",
    "srgb auto linearrgb",
    products="gecko",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG11/painting.html#ColorInterpolationProperty",
)}

${helpers.single_keyword(
    "color-interpolation-filters",
    "linearrgb auto srgb",
    products="gecko",
    gecko_constant_prefix="NS_STYLE_COLOR_INTERPOLATION",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG11/painting.html#ColorInterpolationFiltersProperty",
)}

${helpers.predefined_type(
    "fill",
    "SVGPaint",
    "crate::values::computed::SVGPaint::black()",
    products="gecko",
    animation_value_type="IntermediateSVGPaint",
    boxed=True,
    spec="https://www.w3.org/TR/SVG2/painting.html#SpecifyingFillPaint",
)}

${helpers.predefined_type(
    "fill-opacity",
    "SVGOpacity",
    "Default::default()",
    products="gecko",
    animation_value_type="ComputedValue",
    spec="https://svgwg.org/svg2-draft/painting.html#FillOpacity",
)}

${helpers.predefined_type(
    "fill-rule",
    "FillRule",
    "Default::default()",
    needs_context=False,
    products="gecko",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG11/painting.html#FillRuleProperty",
)}

${helpers.single_keyword(
    "shape-rendering",
    "auto optimizespeed crispedges geometricprecision",
    products="gecko",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG11/painting.html#ShapeRenderingProperty",
)}

${helpers.predefined_type(
    "stroke",
    "SVGPaint",
    "Default::default()",
    products="gecko",
    animation_value_type="IntermediateSVGPaint",
    boxed=True,
    spec="https://www.w3.org/TR/SVG2/painting.html#SpecifyingStrokePaint",
)}

${helpers.predefined_type(
    "stroke-width",
    "SVGWidth",
    "computed::SVGWidth::one()",
    products="gecko",
    animation_value_type="crate::values::computed::SVGWidth",
    spec="https://www.w3.org/TR/SVG2/painting.html#StrokeWidth",
)}

${helpers.single_keyword(
    "stroke-linecap",
    "butt round square",
    products="gecko",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG11/painting.html#StrokeLinecapProperty",
)}

${helpers.single_keyword(
    "stroke-linejoin",
    "miter round bevel",
    products="gecko",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG11/painting.html#StrokeLinejoinProperty",
)}

${helpers.predefined_type(
    "stroke-miterlimit",
    "NonNegativeNumber",
    "From::from(4.0)",
    products="gecko",
    animation_value_type="crate::values::computed::NonNegativeNumber",
    spec="https://www.w3.org/TR/SVG2/painting.html#StrokeMiterlimitProperty",
)}

${helpers.predefined_type(
    "stroke-opacity",
    "SVGOpacity",
    "Default::default()",
    products="gecko",
    animation_value_type="ComputedValue",
    spec="https://svgwg.org/svg2-draft/painting.html#StrokeOpacity",
)}

${helpers.predefined_type(
    "stroke-dasharray",
    "SVGStrokeDashArray",
    "Default::default()",
    products="gecko",
    animation_value_type="crate::values::computed::SVGStrokeDashArray",
    spec="https://www.w3.org/TR/SVG2/painting.html#StrokeDashing",
)}

${helpers.predefined_type(
    "stroke-dashoffset",
    "SVGLength",
    "computed::SVGLength::zero()",
    products="gecko",
    animation_value_type="ComputedValue",
    spec="https://www.w3.org/TR/SVG2/painting.html#StrokeDashing",
)}

// Section 14 - Clipping, Masking and Compositing
${helpers.predefined_type(
    "clip-rule",
    "FillRule",
    "Default::default()",
    needs_context=False,
    products="gecko",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG11/masking.html#ClipRuleProperty",
)}

${helpers.predefined_type(
    "marker-start",
    "url::UrlOrNone",
    "computed::url::UrlOrNone::none()",
    products="gecko",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG2/painting.html#VertexMarkerProperties",
)}

${helpers.predefined_type(
    "marker-mid",
    "url::UrlOrNone",
    "computed::url::UrlOrNone::none()",
    products="gecko",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG2/painting.html#VertexMarkerProperties",
)}

${helpers.predefined_type(
    "marker-end",
    "url::UrlOrNone",
    "computed::url::UrlOrNone::none()",
    products="gecko",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG2/painting.html#VertexMarkerProperties",
)}

${helpers.predefined_type(
    "paint-order",
    "SVGPaintOrder",
    "computed::SVGPaintOrder::normal()",
    products="gecko",
    animation_value_type="discrete",
    spec="https://www.w3.org/TR/SVG2/painting.html#PaintOrder",
)}

${helpers.predefined_type(
    "-moz-context-properties",
    "MozContextProperties",
    "computed::MozContextProperties::default()",
    products="gecko",
    animation_value_type="none",
    spec="Nonstandard (https://developer.mozilla.org/en-US/docs/Web/CSS/-moz-context-properties)",
)}

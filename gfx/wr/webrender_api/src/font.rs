/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use app_units::Au;
#[cfg(target_os = "macos")]
use core_foundation::string::CFString;
#[cfg(target_os = "macos")]
use core_graphics::font::CGFont;
use peek_poke::PeekPoke;
#[cfg(target_os = "macos")]
use serde::de::{self, Deserialize, Deserializer};
#[cfg(target_os = "macos")]
use serde::ser::{Serialize, Serializer};
use std::cmp::Ordering;
use std::hash::{Hash, Hasher};
#[cfg(not(target_os = "macos"))]
use std::path::PathBuf;
use std::sync::Arc;
// local imports
use crate::api::IdNamespace;
use crate::color::ColorU;
use crate::units::LayoutPoint;


#[cfg(not(target_os = "macos"))]
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct NativeFontHandle {
    pub path: PathBuf,
    pub index: u32,
}

#[cfg(target_os = "macos")]
#[derive(Clone)]
pub struct NativeFontHandle(pub CGFont);

#[cfg(target_os = "macos")]
impl Serialize for NativeFontHandle {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        self.0
            .postscript_name()
            .to_string()
            .serialize(serializer)
    }
}

#[cfg(target_os = "macos")]
impl<'de> Deserialize<'de> for NativeFontHandle {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let postscript_name: String = Deserialize::deserialize(deserializer)?;

        match CGFont::from_name(&CFString::new(&*postscript_name)) {
            Ok(font) => Ok(NativeFontHandle(font)),
            Err(_) => Err(de::Error::custom(
                "Couldn't find a font with that PostScript name!",
            )),
        }
    }
}

#[repr(C)]
#[derive(Copy, Clone, Deserialize, Serialize, Debug)]
pub struct GlyphDimensions {
    pub left: i32,
    pub top: i32,
    pub width: i32,
    pub height: i32,
    pub advance: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, Ord, PartialOrd)]
pub struct FontKey(pub IdNamespace, pub u32);

impl FontKey {
    pub fn new(namespace: IdNamespace, key: u32) -> FontKey {
        FontKey(namespace, key)
    }
}

/// Container for the raw data describing a font. This might be a stream of
/// bytes corresponding to a downloaded font, or a handle to a native font from
/// the operating system.
///
/// Note that fonts need to be instantiated before being used, which involves
/// assigning size and various other options. The word 'template' here is
/// intended to distinguish this data from instance-specific data.
#[derive(Clone)]
pub enum FontTemplate {
    Raw(Arc<Vec<u8>>, u32),
    Native(NativeFontHandle),
}

#[repr(u8)]
#[derive(Debug, Copy, Clone, Hash, Eq, MallocSizeOf, PartialEq, Serialize, Deserialize, Ord, PartialOrd, PeekPoke)]
pub enum FontRenderMode {
    Mono = 0,
    Alpha,
    Subpixel,
}

impl Default for FontRenderMode {
    fn default() -> Self {
        FontRenderMode::Mono
    }
}

impl FontRenderMode {
    // Combine two font render modes such that the lesser amount of AA limits the AA of the result.
    pub fn limit_by(self, other: FontRenderMode) -> FontRenderMode {
        match (self, other) {
            (FontRenderMode::Subpixel, _) | (_, FontRenderMode::Mono) => other,
            _ => self,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, MallocSizeOf, PartialOrd, Deserialize, Serialize)]
pub struct FontVariation {
    pub tag: u32,
    pub value: f32,
}

impl Ord for FontVariation {
    fn cmp(&self, other: &FontVariation) -> Ordering {
        self.tag.cmp(&other.tag)
            .then(self.value.to_bits().cmp(&other.value.to_bits()))
    }
}

impl PartialEq for FontVariation {
    fn eq(&self, other: &FontVariation) -> bool {
        self.tag == other.tag &&
        self.value.to_bits() == other.value.to_bits()
    }
}

impl Eq for FontVariation {}

impl Hash for FontVariation {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.tag.hash(state);
        self.value.to_bits().hash(state);
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Hash, Eq, PartialEq, PartialOrd, Ord, Serialize, PeekPoke)]
pub struct GlyphOptions {
    pub render_mode: FontRenderMode,
    pub flags: FontInstanceFlags,
}

impl Default for GlyphOptions {
    fn default() -> Self {
        GlyphOptions {
            render_mode: FontRenderMode::Subpixel,
            flags: FontInstanceFlags::empty(),
        }
    }
}

bitflags! {
    #[repr(C)]
    #[derive(Deserialize, MallocSizeOf, Serialize, PeekPoke)]
    pub struct FontInstanceFlags: u32 {
        // Common flags
        const SYNTHETIC_BOLD    = 1 << 1;
        const EMBEDDED_BITMAPS  = 1 << 2;
        const SUBPIXEL_BGR      = 1 << 3;
        const TRANSPOSE         = 1 << 4;
        const FLIP_X            = 1 << 5;
        const FLIP_Y            = 1 << 6;
        const SUBPIXEL_POSITION = 1 << 7;

        // Windows flags
        const FORCE_GDI         = 1 << 16;

        // Mac flags
        const FONT_SMOOTHING    = 1 << 16;

        // FreeType flags
        const FORCE_AUTOHINT    = 1 << 16;
        const NO_AUTOHINT       = 1 << 17;
        const VERTICAL_LAYOUT   = 1 << 18;
        const LCD_VERTICAL      = 1 << 19;
    }
}

impl Default for FontInstanceFlags {
    #[cfg(target_os = "windows")]
    fn default() -> FontInstanceFlags {
        FontInstanceFlags::SUBPIXEL_POSITION
    }

    #[cfg(target_os = "macos")]
    fn default() -> FontInstanceFlags {
        FontInstanceFlags::SUBPIXEL_POSITION |
        FontInstanceFlags::FONT_SMOOTHING
    }

    #[cfg(not(any(target_os = "macos", target_os = "windows")))]
    fn default() -> FontInstanceFlags {
        FontInstanceFlags::SUBPIXEL_POSITION
    }
}


#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Hash, Eq, MallocSizeOf, PartialEq, PartialOrd, Ord, Serialize)]
pub struct SyntheticItalics {
    // Angle in degrees (-90..90) for synthetic italics in 8.8 fixed-point.
    pub angle: i16,
}

impl SyntheticItalics {
    pub const ANGLE_SCALE: f32 = 256.0;

    pub fn from_degrees(degrees: f32) -> Self {
        SyntheticItalics { angle: (degrees.max(-89.0).min(89.0) * Self::ANGLE_SCALE) as i16 }
    }

    pub fn to_degrees(self) -> f32 {
        self.angle as f32 / Self::ANGLE_SCALE
    }

    pub fn to_radians(self) -> f32 {
        self.to_degrees().to_radians()
    }

    pub fn to_skew(self) -> f32 {
        self.to_radians().tan()
    }

    pub fn enabled() -> Self {
        Self::from_degrees(14.0)
    }

    pub fn disabled() -> Self {
        SyntheticItalics { angle: 0 }
    }

    pub fn is_enabled(self) -> bool {
        self.angle != 0
    }
}

impl Default for SyntheticItalics {
    fn default() -> Self {
        SyntheticItalics::disabled()
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Hash, Eq, PartialEq, PartialOrd, Ord, Serialize)]
pub struct FontInstanceOptions {
    pub render_mode: FontRenderMode,
    pub flags: FontInstanceFlags,
    /// When bg_color.a is != 0 and render_mode is FontRenderMode::Subpixel,
    /// the text will be rendered with bg_color.r/g/b as an opaque estimated
    /// background color.
    pub bg_color: ColorU,
    pub synthetic_italics: SyntheticItalics,
}

impl Default for FontInstanceOptions {
    fn default() -> FontInstanceOptions {
        FontInstanceOptions {
            render_mode: FontRenderMode::Subpixel,
            flags: Default::default(),
            bg_color: ColorU::new(0, 0, 0, 0),
            synthetic_italics: SyntheticItalics::disabled(),
        }
    }
}

#[cfg(target_os = "windows")]
#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Hash, Eq, MallocSizeOf, PartialEq, PartialOrd, Ord, Serialize)]
pub struct FontInstancePlatformOptions {
    pub gamma: u16, // percent
    pub contrast: u16, // percent
}

#[cfg(target_os = "windows")]
impl Default for FontInstancePlatformOptions {
    fn default() -> FontInstancePlatformOptions {
        FontInstancePlatformOptions {
            gamma: 180, // Default DWrite gamma
            contrast: 100,
        }
    }
}

#[cfg(target_os = "macos")]
#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Hash, Eq, MallocSizeOf, PartialEq, PartialOrd, Ord, Serialize)]
pub struct FontInstancePlatformOptions {
    pub unused: u32,
}

#[cfg(target_os = "macos")]
impl Default for FontInstancePlatformOptions {
    fn default() -> FontInstancePlatformOptions {
        FontInstancePlatformOptions {
            unused: 0,
        }
    }
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, PartialOrd, Ord, Serialize)]
pub enum FontLCDFilter {
    None,
    Default,
    Light,
    Legacy,
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, PartialOrd, Ord, Serialize)]
pub enum FontHinting {
    None,
    Mono,
    Light,
    Normal,
    LCD,
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Hash, Eq, MallocSizeOf, PartialEq, PartialOrd, Ord, Serialize)]
pub struct FontInstancePlatformOptions {
    pub lcd_filter: FontLCDFilter,
    pub hinting: FontHinting,
}

#[cfg(not(any(target_os = "macos", target_os = "windows")))]
impl Default for FontInstancePlatformOptions {
    fn default() -> FontInstancePlatformOptions {
        FontInstancePlatformOptions {
            lcd_filter: FontLCDFilter::Default,
            hinting: FontHinting::LCD,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq, Ord, PartialOrd, MallocSizeOf, PeekPoke)]
#[derive(Deserialize, Serialize)]
pub struct FontInstanceKey(pub IdNamespace, pub u32);

impl FontInstanceKey {
    pub fn new(namespace: IdNamespace, key: u32) -> FontInstanceKey {
        FontInstanceKey(namespace, key)
    }
}

/// Data corresponding to an instantiation of a font, with size and
/// other options specified.
///
/// Note that the actual font is stored out-of-band in `FontTemplate`.
#[derive(Clone)]
pub struct FontInstanceData {
    pub font_key: FontKey,
    pub size: Au,
    pub options: Option<FontInstanceOptions>,
    pub platform_options: Option<FontInstancePlatformOptions>,
    pub variations: Vec<FontVariation>,
}

pub type GlyphIndex = u32;

#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub struct GlyphInstance {
    pub index: GlyphIndex,
    pub point: LayoutPoint,
}

impl Default for GlyphInstance {
    fn default() -> Self {
        GlyphInstance {
            index: 0,
            point: LayoutPoint::zero(),
        }
    }
}

impl Eq for GlyphInstance {}

impl Hash for GlyphInstance {
    fn hash<H: Hasher>(&self, state: &mut H) {
        // Note: this is inconsistent with the Eq impl for -0.0 (don't care).
        self.index.hash(state);
        self.point.x.to_bits().hash(state);
        self.point.y.to_bits().hash(state);
    }
}

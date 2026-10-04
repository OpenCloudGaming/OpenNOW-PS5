# OpenNOW PS5 artwork

These images were generated with the built-in image generation tool. They are artwork for this independent PS5 adaptation, not official OpenNOW, Sony or NVIDIA branding. Upstream project credits remain in the main README and third-party notices.

The editable PNG masters are preserved. Operational exports only resize the icon to 512 × 512 and the background to 3840 × 2160. The generated background master is 1672 × 941; the 4K export is an upscale, not native 4K generation. Native launcher backgrounds use BC7_UNORM DX10 DDS without mipmaps. A single background is used for selection and launch.

The active build assets are `sce_sys/icon0.png`, `sce_sys/pic0.dds` and `sce_sys/pic1.dds`. The previous prototype icon is preserved as `opennow-prototype-icon.png`. These changes are prepared for future builds; the existing `0.0.1-alpha` package and installed console files retain their previously verified artwork.

DDS format conversion uses the public [PSGFX imaging pipeline](https://github.com/elripalda/psgfx/blob/a9d73d9fea3382955476fd56ec3c74e08ea2e0ab/src/imaging.h), pinned at `a9d73d9fea3382955476fd56ec3c74e08ea2e0ab`, with stb image loading and bc7enc. The converter is a host tool and is not linked into the PS5 app. Encoded output is decoded for visual inspection and checked with `bash tools/validate-assets.sh`.

## Icon prompt

```text
Use case: logo-brand.
Asset type: square native PS5 home-screen app icon for OpenNOW PS5, also used in the GitHub README.
Primary request: design an original, polished raster app logo for this independent native cloud-gaming prototype, upgrading its existing dark-charcoal, white and mint-green identity. Produce one standalone square image, 1024 by 1024 pixels.
Scene/backdrop: full-bleed very dark charcoal (#10161C), quiet and nearly flat, with a restrained soft mint glow immediately behind the central mark.
Subject: a large, bold, elegantly engineered geometric ON monogram, combining an open circular O and angular N, with an understated forward-play cutout suggesting real-time cloud streaming. A simple distinct silhouette, precision geometry, strong negative space, mint green (#56E69E) and white (#F4F7FF). This is original branding for the PS5 adaptation, not a reproduction of another company's logo.
Style: refined flat graphic design with very subtle light, clean edges, no excessive effects. Icon readable at 64 pixels, with the monogram filling roughly the central two-thirds. Keep margins around the mark.
Text (verbatim): "OPENNOW", one concise line in bold clean uppercase sans-serif below the mark, inside the lower quarter; large enough to read, correctly spelled O P E N N O W.
Constraints: square 1:1 artwork; no mockup, no device, no controller, no photographic scene, no border, no rounded exterior corners, no extra badges, no claims about 4K/120/HDR, no Sony/PlayStation/NVIDIA marks, no watermark. Only the requested word OPENNOW. Opaque background.
```

## Background prompt

The generated icon was supplied as an identity reference.

```text
Use case: ads-marketing.
Asset type: one widescreen 16:9 background for the OpenNOW PS5 native home-screen selected-app and launch views, also displayed as the GitHub README hero artwork. Target 3840 by 2160 pixels.
Input image 1: style and logo identity reference ONLY. The reference is the newly created OpenNOW icon. Create a new wide composition; do not render a screenshot of a PS5 menu.
Primary request: create a refined cloud-streaming background that matches the icon's dark charcoal, mint green and off-white visual identity. Use the same distinctive ON monogram: open mint O with a play triangle and angular white N.
Scene/backdrop: an almost-black charcoal environment with restrained mint light trails flowing into a subtle luminous streaming portal around the emblem, delicate translucent layers and a soft glow; a clean high-quality graphic, no physical device or photographic props.
Composition: the left 45 percent is quiet dark negative space, with no text, no prominent light streaks and no objects, reserved for PS5's own title and Play button. Place the matched emblem and the exact OPENNOW wordmark in the right half, centered around x=72 percent and y=46 percent. Keep a generous margin from every edge. The monogram is prominent but does not fill the screen; horizontal light motion provides a sense of responsive cloud streaming.
Text (verbatim): "OPENNOW" beneath the emblem on the right, clean bold sans-serif, OPEN white and NOW mint, correctly spelled with both N letters. No other text.
Style: polished original app identity artwork, understated and crisp, gentle dimensional lighting with smooth gradients, sufficient dark space for launcher overlays. Preserve the reference logo's recognisable silhouette and palette.
Constraints: one landscape image in exact 16:9 composition; no framing, no border, no rounded tile, no UI screenshot, no controller, no console, no gaming characters, no Sony/PlayStation/NVIDIA marks, no slogans, no watermark, no claims about FPS or HDR. Opaque background.
```

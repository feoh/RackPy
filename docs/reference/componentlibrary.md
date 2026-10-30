# Component library

`rackpy.raw.componentlibrary` is `rack::componentlibrary`: Rack's knobs,
sliders, switches, buttons, ports, screws and lights. **Implemented (RP-28C),
Linux x86_64 inside Rack Free 2.6.6** (see the
[coverage report](coverage.md)). The audio and MIDI buttons come with Rack's
displays (RP-28D).

The components draw Rack's own artwork, which Rack installs: their C++
constructors load it from Rack's folder (`asset::system`), and RackPy copies
none of it. Use them as Rack's C++ plugins do, with the
[factories](app.md#factories):

```{rackpy-conceptual}
self.add_param(raw.create_param_centered(cl.RoundBlackKnob, Vec(30, 60), module, 0))
self.add_input(raw.create_input_centered(cl.PJ301MPort, Vec(30, 300), module, 0))
self.add_child(raw.create_light_centered(cl.MediumLight[cl.RedLight], Vec(30, 120), module, 0))
```

(cl-components)=
## Components

All are Rack's own classes, subclassable from Python:

- **Knobs:** `RoundKnob` and its black sizes, `RoundBlackSnapKnob`,
  `Davies1900hKnob` and its colours, `Rogan` and its 27 variants,
  `SynthTechAlco`, `Trimpot`, `BefacoBigKnob` and `BefacoTinyKnob`. Their
  `bg` (and Rogan's `fg`) are Rack's own SvgWidget children, as
  `HostSvgWidget`s.
- **Sliders:** `BefacoSlidePot`, `VCVSlider` (`LEDSlider`).
- **Switches and buttons:** `NKK`, `CKSS`, `CKSSThree`,
  `CKSSThreeHorizontal`, `CKD6`, `TL1105`, `VCVButton` (`LEDButton`),
  `VCVLatch`, `BefacoSwitch`, `BefacoPush`, `VCVBezel` (`LEDBezel`),
  `VCVBezelLatch`, `PB61303`.
- **Ports:** `PJ301MPort`, `DarkPJ301MPort`, `ThemedPJ301MPort`,
  `PJ3410Port`, `CL1362Port`.
- **Screws:** `ScrewSilver`, `ScrewBlack`, `ThemedScrew`.
- `SegmentDisplay`: `lights_len`, `vertical`, `margin`, and
  `set_lights(module, first_light_id, lights_len, light_base=WhiteLight)`.
  That is Rack's `setLights<TLightBase>`, which fills the display with
  `RectangleLight[light_base]` segments.
- The colours `SCHEME_BLACK` to `SCHEME_DARK_GRAY` are `NVGcolor`s.

(cl-templates)=
## Templates

The library composes lights, light sliders and light buttons with C++
templates: `MediumLight<RedLight>`, `VCVLightSlider<GreenLight>`,
`LightButton<VCVButton, WhiteLight>`. Each is a *template class*
(`componentlibrary.Template`) that holds the template's own code, Rack's
header code, and subscripting it gives the specialization:

- `MediumLight[RedLight]` is a class deriving from `MediumLight` and
  `RedLight`, which shows Rack's `MediumLight.svg`. The same arguments always
  give the same class, and `isinstance(light, MediumLight)` holds for every
  specialization. A template class itself cannot be instantiated, as in C++.
- Rack's aliases are the default specializations: `RedLight` is
  `TRedLight[GrayModuleLightWidget]`, which is also `TRedLight.default()`.
- Rack's alias templates are `AliasTemplate`s that give the aliased
  specialization itself: `VCVLightButton[RedLight]` is
  `LightButton[VCVButton, RedLight]`.
- A template's arguments are classes, Python subclasses included. Templates
  without defaults need them: `LargeLight[...]`, `RectangleLight[...]`.

The templates:

- `TSvgLight` (`SvgLight`), which has `fb`, `sw` and `set_svg()`.
- `TGrayModuleLightWidget` (`GrayModuleLightWidget`).
- The colour templates: `TWhiteLight`, `TRedLight`, `TGreenLight`,
  `TBlueLight`, `TYellowLight`, `TGreenRedLight` and `TRedGreenBlueLight`,
  each with its alias (`WhiteLight`, ...). Their constructors add the named
  scheme colours to the selected base, including a Python subclass base.
- `LargeLight`, `MediumLight`, `SmallLight` and `TinyLight` (Rack's SVGs).
- `LargeSimpleLight`, `MediumSimpleLight`, `SmallSimpleLight` and
  `TinySimpleLight` (sizes in mm).
- `RectangleLight`, which draws a rectangular background and light.
- `VCVBezelLight` (`LEDBezelLight`) and `PB61303Light`.
- `LightSlider[base, light]`, `VCVSliderLight` (`LEDSliderLight`) and
  `VCVLightSlider` (`LEDLightSlider`). The deprecated `LEDSliderGreen`,
  `LEDSliderRed`, `LEDSliderYellow`, `LEDSliderBlue` and `LEDSliderWhite` are
  classes, as in Rack.
- `MomentarySwitch`, `LightButton[base, light]`, `VCVLightButton`
  (`LEDLightButton`), `VCVLightLatch`, `VCVLightBezel` (`LEDLightBezel`) and
  `VCVLightBezelLatch`.

The light sliders and buttons have a `light` and `get_light()`, which
[`create_light_param`](app.md#factories) uses:

```{rackpy-conceptual}
button = raw.create_light_param_centered(cl.VCVLightButton[cl.MediumSimpleLight[cl.RedLight]], pos, module, 0, 0)
```

As in C++, a light button's light keeps Widget's infinite default size
unless its class sets one, so pick a sized light, as above.

# Shot Flow Profile Format

Shot Flow stores target profiles as `openscale.shot-profile/v1` JSON:

```json
{
  "schema": "openscale.shot-profile/v1",
  "id": "house-espresso",
  "name": "House espresso",
  "ratio": 2,
  "duration": 25,
  "profile": [[0, 0], [0.2, 1], [1, 0]]
}
```

`profile` contains `[time, flow]` pairs normalized to 0-1 time and relative flow. Shot Flow scales the curve so its integral equals `dose * ratio`; the yield curve is therefore the integral of the displayed target flow.

Convert a DE1 legacy Tcl profile to this format:

```sh
node tools/convert_shot_profile.mjs to-scale profile.tcl profile.json --dose 18
```

Convert back to a simplified DE1 advanced flow profile:

```sh
node tools/convert_shot_profile.mjs to-de1 profile.json profile.tcl --dose 18 --maximum-flow 4
```

Conversion preserves title, stage timing, flow targets, pressure-stage flow limiters, target weight, and optional provenance. DE1 pressure, temperature, exit conditions, and adaptive behavior have no scale-profile equivalent and are not round-tripped. The reverse conversion maps normalized beverage-output flow to DE1 machine input flow, so the generated profile is a starting point that must be reviewed on the machine.
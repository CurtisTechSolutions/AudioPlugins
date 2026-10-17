# Third-party notices

This repository is licensed under the GPL-3.0 (see `LICENSE`, the organisation's
default). Curtis Tech Solutions holds the copyright on all first-party code and
can relicense it for a commercial release at any time; the notes below cover
the components that come with their own terms.

- **JUCE** (https://juce.com) — fetched at build time, not vendored. Available
  under the JUCE End User Licence Agreement (https://juce.com/legal/juce-9-licence/),
  which has revenue-based tiers, or under the AGPLv3. Nothing in this repo
  modifies JUCE.
- **VST3 SDK** — bundled inside JUCE. Copyright (c) Steinberg Media Technologies
  GmbH. Available under the GPLv3 or Steinberg's proprietary VST 3 licence;
  distributing VST3 plugins requires accepting Steinberg's VST 3 licence
  agreement (https://www.steinberg.net/en/company/developers.html).
- **pluginval** (https://github.com/Tracktion/pluginval) — used in CI only to
  validate the built plugins; not part of the distributed binaries.

The DSP in `core/` is written from scratch for this project. The pitch detector
implements YIN (de Cheveigné & Kawahara, 2002); the pitch shifter implements
PSOLA (Moulines & Charpentier, 1990). Both are published algorithms, not code
taken from other implementations.

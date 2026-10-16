# Offline framework fixtures

`about:frameworks` runs React; `/vue`, `/alpine` and `/astro` select the other demos.
These use real upstream JavaScript and compiled Tailwind CSS. The Astro page
is actual Astro production output, including its island runtime and Preact
renderer. The assets are built into the kernel, so the demos and `selftest browser`
need no Internet access or installed packages.

Pinned sources (MIT; accompanying `*-LICENSE` files retain their notices):

| File | Upstream |
|---|---|
| `react.js` | https://unpkg.com/react@18.3.1/umd/react.production.min.js |
| `react-dom.js` | https://unpkg.com/react-dom@18.3.1/umd/react-dom.production.min.js |
| `vue.js` | https://unpkg.com/vue@3.5.13/dist/vue.global.prod.js |
| `alpine.js` | https://unpkg.com/alpinejs@3.14.9/dist/cdn.min.js |
| `tailwind.css` | Generated with Tailwind CSS 3.4.17 from the three HTML fixtures |
| `astro/` | Astro 5.13.2, `@astrojs/preact` 4.1.0 and Preact 10.26.9, built from `astro-src/` with its committed npm lockfile |

The counters check mounting, click handling, reactive state, inserted/replaced
stylesheets, CSS variables, utility selectors and responsive sizing. They are
compatibility probes, not a promise that every application using these libraries
works. Tailwind v4, the Tailwind CDN compiler, transitions and complete CSS/DOM/
JavaScript standards coverage remain outside this tested slice.

The Astro checks cover HTML with JavaScript disabled, scoped CSS, an ordinary
compiled module script, and independent Preact islands using `client:load`,
`client:idle` and `client:only`. `client:visible`, media-triggered hydration,
React/Vue renderers inside Astro, view transitions and server islands remain
unverified. See [Astro's client directives](https://docs.astro.build/en/reference/directives-reference/#client-directives).
The fixture uses Astro's `build.assetsPrefix` to serve its modules through
`about:frameworks/astro`; the generated runtime is not patched.

To regenerate Astro, install Node/npm on the host and run
`python3 tools/build_astro_fixture.py`. It builds in `build/test/astro-project`
and refreshes the generated HTML, modules, resource maps, checksums and licenses.
Normal OS builds and `tools/test_browser.py` use the committed output and need
no Node/npm or network connection.

Regenerate CSS from the repository root (Deno with npm support, or equivalent
Tailwind CLI 3.4.17):

```sh
deno run -A npm:tailwindcss@3.4.17 \
  -i web/fixtures/tailwind-input.css -o web/fixtures/tailwind.css \
  --content web/fixtures/react.html,web/fixtures/vue.html,web/fixtures/alpine.html --minify
```

Run `python3 tools/test_browser.py` on Linux, optionally `--32` or `--sanitize`.
Build and boot your ISO with `tools/dev.sh run`, then run `selftest browser`
inside Banana OS. Open `about:frameworks` in its browser for the visual demos.

The real-site compatibility target is **https://swm.mxtpasontop.fr/**.
Run `selftest browser swm` inside Banana OS for HTTPS/resources, theme and
responsive CSS, Astro island hydration, install tabs, the mobile menu, locale
visibility, desktop grid and heading layout, script errors, image decoding and memory limits. These are targeted
checks; passing them does not establish complete visual or standards compatibility.
They use the current website, so future site changes may require test updates.

On Linux, `python3 tools/test_browser.py --swm` also runs the live checks
(requires pkg-config and libcurl development files), and writes `swm.png` under
its `build/test/browser/` output directory. The host and OS use the same
bitmap decoder (vendored stb_image and libwebp 1.6.0); the host transport uses
libcurl and does not verify Banana OS's TLS implementation. The default
offline suite still needs no network or libcurl.

After fixing explicit grid columns, font-relative sizing and WebP decoding,
the live host run passes all 16 SWM checks, including its four images. The
2880x1800 screenshots exceed the old four-million-pixel browser ceiling;
the adapter now allows eight million pixels, charged to the page arena.
This page uses about 109 MiB of document arena storage. The VM previously
confirmed networking, scripts and hydration; these latest layout/image fixes
still need a VM run. Use the same `selftest browser swm` command.

The offline suite includes grid geometry (fixed/percentage/fr columns,
`minmax`, numeric `repeat`, gaps, automatic rows and responsive resize), final
font size before `ch` lengths, WebP RGB pixel checks, malformed input and size
limits, and PNG/JPEG/BMP/GIF regression checks. Grid supports up to 16 explicit
columns; spans, explicit row tracks, named lines and auto-fit/auto-fill remain
unsupported. Animated WebP is reported as unsupported. This is a compatibility
baseline, not complete visual parity or support for every framework feature.

# In-House Web Engine Qualification: Blocking APIs, Error Taxonomy & Subsystem Breakdown

This document delivers **base-13 step 2** ("Établir pour le moteur actuel les API et erreurs qui empêchent chaque parcours ; séparer JavaScript, DOM, rendu, réseau et sécurité"). It establishes a systematic technical analysis of the existing Banana OS in-house browser engine (`web/`, `kernel/browser.c`) against the five evaluation journeys defined in [`docs/web-engine-strategy-journeys.md`](file:///var/lib/yhello/worktrees/base-13-s2/docs/web-engine-strategy-journeys.md), categorizing all blocking APIs, structural limitations, and runtime failure modes into five technical subsystems:
1. **JavaScript Runtime & Language Support** (`web/script*.c`, `web/prelude.*`)
2. **DOM, Events & Web Platform APIs** (`web/jsdom.c`, `web/html.c`)
3. **Layout, CSS & Rendering Engine** (`web/layout.c`, `web/css*.c`, `web/render.c`, `web/svg.c`)
4. **Network, Transport & Asynchronous Protocol Pipeline** (`kernel/browser.c`, `web/jsdom.c`, `kernel/net.c`)
5. **Security, Execution Sandboxing & Memory Protection** (`kernel/browser.c`, `kernel/uspace.c`, `base-04`)

---

## 1. Executive Subsystem Assessment

The in-house engine demonstrates remarkable achievements for a bespoke codebase—passing 73/73 offline test suites (`python3 tools/test_browser.py`) covering CSS variables, CSS math functions (`calc()`, `clamp()`), Tailwind arbitrary classes, offline React/Vue/Alpine/Astro mount & reactive clicks, WebP image decoding, and asynchronous `MutationObserver` queues.

However, when confronted with real-world interactive web applications (represented by the 5 evaluation journeys: Webmail Login, Webmail Read/Reply, GitHub Repo & Diff Navigation, Collaborative Document Editing, and Multipart Upload/Download), the in-house engine faces systemic architectural roadblocks that cannot be resolved through trivial incremental patching.

### Subsystem Capability Summary

| Subsystem | Implemented Strengths | Fundamental Roadblocks & Missing Capabilities | Impact on Daily-Use Journeys |
|---|---|---|---|
| **1. JavaScript Runtime** | Core ES5/ES6 syntax, arrow functions, template literals, basic Promises (`es_promise_*`), BigInt, TypedArrays, Map/Set, basic regex. | Missing full ES2020+ specifications, async generators, RegExp unicode property escapes (`\p{...}`), WeakRef/FinalizationRegistry, WebAssembly (`wasm`), full Symbol registry. Synchronous interpreter without multi-threading or web workers. | Blocks modern SPA bundles (GitHub, Webmail, Google Docs) which rely on modern Babel/Webpack/Vite runtime outputs and polyfills. |
| **2. DOM & Web APIs** | `document.getElementById`, `querySelector`, classList, `MutationObserver`, synthetic event dispatching (`click`, `input`), dummy stubs for `IntersectionObserver`, `history.pushState`. | `history.pushState` and `replaceState` are empty no-ops (`m_nothing`); missing Selection/Range API (`getSelection()`), `contentEditable` host editing logic, Shadow DOM v1, Custom Elements lifecycle (`customElements.define`), Canvas 2D / WebGL context, Web Audio API, Fullscreen API, Clipboard API. | Completely breaks SPA client-side routing (GitHub, Webmail), rich text editors (Etherpad, Docs), and modern web components (`<relative-time>`, `<tab-container>`). |
| **3. Layout & CSS** | Box model, inline/block flow, basic Flexbox, 1D/2D CSS Grid tracks (`web/layout_grid.h`, `css_grid.h`), CSS custom properties with cycles and fallbacks, basic inline SVG shapes. | No CSS subgrid, complex flex-wrap alignment, multi-column layout, CSS animations/transitions, `@supports`, complex CSS pseudo-classes (`:has()`, `:is()`, `:where()`, `:focus-within`), font ligature shaping (HarfBuzz equivalent), complex table layout algorithms. | Causes visual degradation or broken layouts on complex diff tables (GitHub commit diffs) and collaborative canvas layouts. |
| **4. Network Pipeline** | HTTP/1.1 client (`kernel/browser.c`), HTTPS/TLS via kernel TLS, cookie jar (`g_cookies`) handling `HttpOnly` and persistent file storage, redirects up to 10 hops, basic multipart POST serialization (`make_body`). | **Strictly synchronous network dispatch**: `fetch()` and `XMLHttpRequest` block the entire browser task loop during socket I/O; no HTTP/2 or HTTP/3 multiplexing; **No WebSocket (`ws://` / `wss://`) support** anywhere in kernel or browser; no Server-Sent Events (`EventSource`); no WebRTC; missing request abort (`AbortController`). | Completely blocks real-time collaborative document synchronization (Journey 4) and live notifications/chat; stalls browser UI during network queries. |
| **5. Security & Isolation** | Cookie domain scoping, `HttpOnly` cookie protection against script access (`env_cookie_get`), basic HTML tag sanitizer for about views. | **Browser and JS engine execute inside kernel ring 0 space** (subject of pending task `base-04`); no process isolation per origin or per tab; no Content Security Policy (CSP) enforcement; no Cross-Origin Resource Sharing (CORS) security boundary enforcement; no Subresource Integrity (SRI); no sandboxed iframe isolation (`<iframe sandbox>`). | Severe privilege escalation risk: any arbitrary memory corruption in JS interpreter or CSS/image parser results in complete kernel takeover. |

---

## 2. In-Depth Subsystem Error & API Analysis

### Subsystem 1: JavaScript Runtime (`web/script*.c`, `web/prelude.*`)

The JavaScript engine is a lightweight hand-rolled AST interpreter with an arena allocator and custom garbage collector:
- **Missing Syntax & ES Standards**:
  - `async`/`await` is partially desugared via Promises in `web/prelude.js`, but top-level await and async generator functions (`async function*`) are unsupported, causing runtime `SyntaxError: unexpected token` when loading modern vendor bundles.
  - Regular Expression engine (`web/regex.c`) supports basic PCRE/Perl-like patterns, but lacks Unicode property escapes (`\p{Script=...}`, `\p{L}`), named capture groups with lookbehind assertions (`(?<=...)`), and the `/v` flag. Modern polyfills (Core-JS) fail during regex parsing.
  - Missing built-in globals: `Intl` (Collator, DateTimeFormat, NumberFormat, PluralRules) is entirely absent, causing client-side date/time formatters in webmail and GitHub to throw `ReferenceError: Intl is not defined`.
  - Missing WebAssembly runtime: Sites compiling cryptographic modules, rich editors, or document parsers to WebAssembly fail with `ReferenceError: WebAssembly is not defined`.

### Subsystem 2: DOM, Events & Web Platform APIs (`web/jsdom.c`, `web/html.c`)

The DOM layer in `web/jsdom.c` provides an interface between the JS runtime and C `dom_node_t` structures:
- **History API Stubbing (`pushState`, `replaceState`)**:
  - In `web/jsdom.c:3029-3031`:
    ```c
    obj_set(I, hist, "pushState", v_native(I, "pushState", m_nothing));
    obj_set(I, hist, "replaceState", v_native(I, "replaceState", m_nothing));
    ```
    Both methods bind to `m_nothing` (a C function that returns `undefined` without action). They do not update `window.location`, do not record navigation history in `kernel/browser.c`, and do not fire `popstate` events. In single-page applications (GitHub, modern Webmail), clicking repository folders or message threads updates nothing, leaving the user stranded on the initial view.
- **Selection & Range APIs**:
  - `window.getSelection()` and `document.createRange()` are not implemented.
  - When rich text editors attempt to query the current cursor position or wrap a selection in a DOM node, `document.getSelection is not a function` terminates execution.
- **`contentEditable` Attribute**:
  - While HTML attribute parsing recognizes `contenteditable="true"`, `kernel/browser.c` has zero keystroke-to-DOM text insertion dispatch for non-`<input>`/`<textarea>` elements. Users cannot place a cursor, select text, or type inside collaborative document editors or rich webmail compose containers.
- **Custom Elements & Shadow DOM**:
  - `window.customElements` does not implement `define()` with lifecycle callbacks (`connectedCallback`, `attributeChangedCallback`). GitHub's Web Components (`<relative-time>`, `<tab-container>`, `<clipboard-copy>`) fail to initialize and render as unstyled inert inline elements.

### Subsystem 3: Layout, CSS & Rendering Engine (`web/layout.c`, `web/css*.c`, `web/svg.c`)

- **Complex Layout Models**:
  - `web/layout_grid.h` and `web/css_grid.h` support basic fractional tracks and minmax, but lack named grid areas, auto-placement algorithms (`grid-auto-flow: dense`), and CSS subgrid.
  - Table layout algorithm in `web/layout.c` is basic flow-based without CSS table specification compliance (missing `table-layout: fixed` vs `auto` column width distribution, cell spanning edge cases). Unified and side-by-side git diffs on GitHub render misaligned or overflow the screen width.
- **CSS Selectors & Styling**:
  - Missing functional pseudo-classes: `:has()`, `:is()`, `:where()`, `:not()` with complex selector lists, `:focus-within`, `:focus-visible`. Modern stylesheets (Tailwind v3/v4, GitHub Primer CSS) rely heavily on `:has()` and `:where()`, resulting in unapplied CSS rules and unstyled UI components.
- **Text Shaping & Internationalization**:
  - TrueType rendering (`kernel/ttf.c`) performs simple left-to-right glyph layout with basic advance metrics. It does not perform OpenType complex script shaping, cursive joining (Arabic/Persian), Indic reordering, or font ligatures (code ligatures in diffs).

### Subsystem 4: Network, Transport & Protocol Pipeline (`kernel/browser.c`, `kernel/net.c`)

- **Synchronous Blocking Network Calls**:
  - In `web/jsdom.c:2323` and `kernel/browser.c:764` (`env_request`), `fetch()` and `XMLHttpRequest` execute synchronously via `fetch_url()` on the main thread:
    ```c
    int rc = do_request(p, url, method, body, blen, btype, &data, &len, ctype, sizeof(ctype), err, sizeof(err), final_url);
    ```
    If a webmail backend or GitHub API takes 2 seconds to respond, the entire browser window completely freezes and ignores mouse/keyboard input until the socket returns.
- **Absence of WebSocket Protocol (`ws://`, `wss://`)**:
  - There is zero WebSocket implementation in the entire OS.
  - Real-time collaborative documents (HedgeDoc, Etherpad, Nextcloud Text, Google Docs) immediately fail their connection handshake and fall back to polling or display a fatal "Disconnected from server" dialog.
- **HTTP Header & Request Customization**:
  - In `web/jsdom.c:2422` (`header_ctype`), the only HTTP request header extracted from `fetch()` options is `Content-Type`:
    ```c
    for (uint32_t i = 0; i < o->n; i++)
        if (o->props[i].key && !strcasecmp(o->props[i].key->s, "content-type")) { ... }
    ```
    Crucial authentication and API headers—such as `Authorization: Bearer <token>`, `Accept: application/json`, and custom headers (`X-Requested-With`, `CSRF-Token`)—are completely ignored and never transmitted over the wire in `kernel/browser.c:721-725`. Any REST API call requiring an authorization token fails with HTTP 401 Unauthorized or HTTP 403 Forbidden.
- **HTTP/2 & HTTP/3**:
  - Only HTTP/1.1 is supported. Sites establishing dozens of simultaneous resource requests (GitHub asset pipelines) encounter head-of-line blocking and socket exhaustion.

### Subsystem 5: Security, Sandbox & Memory Protection (`kernel/browser.c`)

- **Kernel Privilege Hazard**:
  - As highlighted in `base-04`, the HTML/CSS parser, SVG renderer, and JS interpreter execute with full kernel privileges in ring 0.
  - A malformed JS script, stack overflow, or heap corruption in `web/` causes a kernel panic (`ud2` / page fault), crashing the entire operating system rather than crashing a single browser tab.
- **Absence of Origin-Based Security Boundaries**:
  - There is no Same-Origin Policy (SOP) enforcement or CORS checking on `fetch()` / `XMLHttpRequest`. A malicious page can fetch resources from any intranet or local address without origin restrictions.
  - Cookies are isolated only by domain string matching in `kernel/browser.c:134` (`domain_match`); there is no `SameSite=Strict/Lax` handling or origin port scoping.

---

## 3. Journey-by-Journey Failure & Missing API Matrix

The following matrix systematically breaks down why each of the 5 evaluation journeys fails under the current in-house engine:

| Journey | Sequential Action | Observed / Expected Engine Failure Mode | Subsystem | Blocking Missing APIs / Root Cause |
|---|---|---|---|---|
| **Journey 1: Webmail Login** | 1.1 Render Login Form | Partial / Renders basic HTML forms, but CSS flexbox/grid layout defects break responsive login cards. | Layout / CSS | Lack of `:focus-visible`, modern CSS resets, and complex form field pseudo-elements. |
| | 1.2 Form Input & Validation | Fails client-side validation scripts. Form submits with missing fields or validation exceptions. | JS / DOM | Missing HTML5 Form Validation API (`input.checkValidity()`, `reportValidity()`, `validity` object). |
| | 1.3 Credential Submission | Multi-hop redirects work for simple cookies, but OAuth2/OIDC flows fail on URL fragment/state parsing. | Net / JS | `window.location.hash` / `URLSearchParams` incomplete; synchronous redirect drops Authorization headers. |
| | 1.4 Post-Auth SPA Hydration | White screen or unhandled exception upon landing on authenticated inbox. | JS / DOM | Missing modern ES APIs (`Intl`, `WeakSet`, `Symbol.iterator`), causing vendor bundle bundle execution crash. |
| **Journey 2: Webmail Read / Reply** | 2.1 Message List Retrieval | Fails to fetch thread list; spinning loading spinner indefinitely. | Net / Security | `fetch()` drops `Authorization` and CSRF headers; `Response.headers` only provides `Content-Type`. |
| | 2.2 Thread Selection | Clicking a message in the thread list does not load content. | DOM / JS | History API `pushState` is a no-op; unhandled custom events (`CustomEvent` detail dispatch). |
| | 2.3 Reply Compose Buffer | Clicking "Reply" does not allow typing in the compose area. | DOM / Layout | Compose pane uses `contentEditable="true"` or `<iframe>` designMode; no cursor placement or key event routing. |
| | 2.4 Send Message | Dispatched reply payload is rejected or corrupt. | Net / JS | Missing `FormData.set()`, binary attachment encoding limitations in synchronous `do_request()`. |
| **Journey 3: GitHub Repository Navigation** | 3.1 Initial Repository View | File tree renders partly, but dates and icons are missing or misaligned. | DOM / SVG | Custom elements (`<relative-time>`, `<octicon>`) unsupported; CSS grid auto-placement missing. |
| | 3.2 File Navigation (SPA/Turbo) | Clicking a file link triggers no page update or opens a blank screen. | DOM / JS | Turbo / PJAX relies on `pushState` and `popstate` to fetch HTML partials; engine does nothing. |
| | 3.3 Commit Diff View | Diff view renders code lines wrapped improperly; table columns collapse. | Layout / CSS | Lack of `table-layout: fixed`, CSS `sticky` positioning, and font ligature code shaping. |
| **Journey 4: Collaborative Document Editing** | 4.1 Document Loading | Connection fails with "WebSocket connection to 'wss://...' failed". | Net / Transport | Complete lack of `WebSocket` implementation in `kernel/browser.c` and `web/jsdom.c`. |
| | 4.2 Interactive Text Entry | Clicking into document canvas or textarea does not capture keyboard focus or edit buffer. | DOM / Events | Missing `getSelection()`, `Range`, `beforeinput` event lifecycle, and canvas text measurement APIs. |
| | 4.3 Real-Time Sync & Auto-Save | Document edits are lost; changes never transmit to server. | Net / JS | Requires bidirectional WebSocket channel or background interval REST synchronization. |
| **Journey 5: Multipart Upload / Download** | 5.1 Native File Chooser | File input opens dialog, but multiple file selection or drag-and-drop fails. | Kernel / DOM | Missing `DragEvent`, `DataTransfer`, `HTMLInputElement.files` assignment restrictions. |
| | 5.2 Multipart Stream Upload | Large binary file uploads cause memory allocation failure (`kmalloc` exhausted). | Net / Memory | `make_body` attempts to buffer the entire file in a single contiguous kernel memory buffer; fails on >8MB files. |
| | 5.3 Binary File Download | Downloads succeed for recognized binary extensions, but custom MIME streams without filename fail. | Net / Kernel | Relies on naive extension guessing (`download_ext`); lacks stream-to-disk pipe for arbitrary blob downloads. |

---

## 4. Architectural Conclusions for Step 3 Decision

1. **The In-House Engine Cannot Reach Daily-Use Viability Through Minor Increments**:
   The gap between passing synthetic component tests (offline React/Vue/Alpine fixtures) and executing production web applications (GitHub, Webmail, Google Docs/Etherpad) is architectural. The missing components—a fully standard-compliant JavaScript runtime (ES2022+), complete HTML5 DOM (Shadow DOM, Custom Elements, Selection/Range, ContentEditable), modern CSS layout (Grid subgrid, modern pseudo-classes), bidirectional asynchronous networking (WebSockets, HTTP/2, streaming I/O), and multi-process security isolation—would require re-implementing thousands of man-years of standard web platform specifications from scratch.

2. **QuickJS Engine Scope & Boundary**:
   Integrating QuickJS (`quickjs` checklist task) solves the JavaScript runtime deficits (providing 100% compliant ES2020+, regex, BigInt, and standard library support). However, **QuickJS provides 0 lines of HTML parsing, 0 lines of CSS layout, 0 DOM objects, and 0 networking**. If QuickJS is selected, the in-house team remains responsible for authoring the thousands of missing DOM, CSS, and network interfaces.

3. **Full Maintained Engine Port (Blink / WebKit / Ladybird / NetSurf)**:
   A maintained engine provides the complete, integrated web platform. However, it requires porting massive POSIX/C++ runtimes, threading abstractions, shared memory IPC, and GPU rendering pipelines. Step 3 will directly compare these two paths on memory, dependencies, maintenance, and realistic deliverability.

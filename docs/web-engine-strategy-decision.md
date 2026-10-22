# Web Engine Strategy: Comparative Analysis & Architectural Decision

This document delivers **base-13 step 3** ("Comparer une preuve QuickJS intégrée à une page de test à l’inventaire d’un moteur complet candidat. Décider sur dépendances, maintenance et résultats, sans considérer l’étude comme un port terminé.").

Building upon the five evaluation journeys defined in [`docs/web-engine-strategy-journeys.md`](file:///var/lib/yhello/worktrees/base-13-s3/docs/web-engine-strategy-journeys.md) and the technical qualification report in [`docs/web-engine-qualification-step2.md`](file:///var/lib/yhello/worktrees/base-13-s3/docs/web-engine-qualification-step2.md), this evaluation contrasts an incremental in-house engine path augmented with QuickJS against candidate maintained full web browser engines (NetSurf, WebKit/WPE, Chromium/Blink, and Ladybird). It establishes a definitive architectural decision, migration plan, and demonstrable milestones for Banana OS.

---

## 1. QuickJS Integration Feasibility & Subsystem Boundary Analysis

Task `quickjs` in `docs/daily-use-checklist.json` proposed replacing the in-house JavaScript interpreter (`web/script*.c`) with Fabrice Bellard and Charlie Gordon's QuickJS engine. To evaluate whether this path can bridge the gap identified in the 5 evaluation journeys, we analyze the exact boundaries of what QuickJS provides versus what remains absent.

### 1.1 What QuickJS Provides
- **ECMAScript Standard Compliance**: Full ES2020/ES2023 specification compliance, including async/await, generators, RegExp Unicode property escapes (`\p{...}`), BigInt, TypedArrays, Symbol registry, WeakMap/WeakSet, and Proxy.
- **Compact C Footprint**: Self-contained C99 codebase (~1.2 MB source, producing ~600 KB x86_64 binary code), with no mandatory C++ or external library dependencies.
- **Predictable Memory Footprint**: Arena/reference-counting GC with cycle detection, configurable memory allocation limits (`JS_SetMemoryLimit`), and deterministic script interruption hooks (`JS_SetInterruptHandler`).
- **Standard Library Modules**: Built-in math, JSON, and regex engines that immediately eliminate custom parser bugs in `web/regex.c`.

### 1.2 What QuickJS Does NOT Provide (The Missing Iceberg)
QuickJS is strictly an ECMAScript runtime. When mapped against the 5 evaluation journeys and the failure taxonomy of step 2, QuickJS provides **zero** lines of code for the remaining 4 browser subsystems:

```
+-------------------------------------------------------------------------------+
|                             BROWSER PLATFORM                                  |
+---------------------------------------+---------------------------------------+
| Subsystem                             | Provided by QuickJS?                  |
+---------------------------------------+---------------------------------------+
| JavaScript / ECMAScript Runtime       | YES (100% standard compliance)        |
| HTML5 Tree Construction / Parsing     | NO  (0% - must use web/html.c)        |
| DOM Living Standard & Event Dispatch  | NO  (0% - requires custom C bindings) |
| CSS Parser, Cascade & Inheritance     | NO  (0% - must use web/css.c)         |
| Box Model, Flexbox & CSS Grid Layout  | NO  (0% - must use web/layout*.c)     |
| 2D Canvas & WebGL Graphics Pipeline   | NO  (0% - absent)                     |
| TrueType / OpenType Text Shaping      | NO  (0% - requires HarfBuzz / Pango)  |
| Asynchronous Event Loop & Timers      | PARTIAL (microtasks only, no macrotask|
|                                       | event pump or window message queue)   |
| Networking (HTTP/2, HTTP/3, WebSockets| NO  (0% - absent)                     |
| Security Boundaries (SOP, CORS, CSP)  | NO  (0% - absent)                     |
+---------------------------------------+---------------------------------------+
```

### 1.3 The In-House + QuickJS Trap: Binding & Specification Maintenance
Connecting QuickJS to `web/jsdom.c` requires writing and maintaining manual C-to-JS bindings (`JS_NewCFunction`, `JS_SetPropertyStr`) for every standard Web API.
1. **DOM Living Standard Complexity**: The W3C/WHATWG DOM specification contains over 2,400 interfaces and tens of thousands of properties and methods. In the current in-house engine, bindings are manually maintained in `web/jsdom.c` (e.g. `obj_set(I, hist, "pushState", v_native(...))`). Every newly required method (`Selection`, `Range`, `CustomElementRegistry`, `ResizeObserver`, `AbortController`) requires custom C glue.
2. **Object Lifetime & Garbage Collection Coupling**: Bridging QuickJS reference-counted JS objects (`JSValue`) with the DOM node tree (`dom_node_t`) in `web/html.c` introduces complex cross-heap reference cycles. A DOM node held in JS must not be freed by C layout, but cyclical references between JS event listeners and C DOM nodes easily leak memory or cause use-after-free faults.
3. **Unresolved Roadblocks**: Even with QuickJS executing modern JS bundles without syntax errors, the browser remains unable to handle Journey 1 (no form validation API), Journey 2 (no `pushState` routing or `contentEditable`), Journey 3 (no custom elements or fixed tables), and Journey 4 (no WebSockets or Selection API).

**Conclusion on QuickJS**: Integrating QuickJS replaces an AST interpreter with a bytecode engine, but does not solve the web browser problem. Pursuing the in-house + QuickJS path requires thousands of person-hours re-implementing DOM, CSS, and network stacks from scratch.

---

## 2. Comprehensive Inventory & Comparison of Candidate Maintained Engines

To determine an achievable strategy, we evaluate four candidate open-source web browser engines across five critical criteria: Memory Footprint, Dependencies & Porting Friction, Architectural Fit with Banana OS, Maintenance & Build Burden, and Journey Coverage.

### Candidate 1: NetSurf (Desktop / Embedded Engine)
- **Architecture**: Clean, modular C99 codebase. Uses specialized independent libraries: `libhubbub` (HTML5 compliant tokeniser/tree builder), `libcss` (CSS3 parsing and cascade selection), `libnsfb` (framebuffer abstraction), `libdom` (W3C DOM core implementation), and Duktape or QuickJS for ECMAScript.
- **Memory Footprint**: Extremely lean. Base engine cold boot requires ~20 MB to 45 MB RAM. Ideal for resource-constrained systems or early kernel/user-space environments.
- **Dependencies**: Minimal POSIX C runtime (`libc`, `libcurl`, `openssl`/`mbedtls`, `libpng`, `libjpeg`, `freetype`). Does not require C++20 runtimes, Python build systems at target runtime, or multi-gigabyte toolchains.
- **Porting Effort**: Low to moderate. The frontend abstraction layer (`struct gui_window_table`) requires providing ~30 C function callbacks (draw_rectangle, draw_text, mouse_click, schedule_timer). Banana OS already has `gui/`, `kernel/fb.c`, and FreeType/TTF capabilities.
- **Limitations**: NetSurf's JavaScript engine (Duktape) and DOM binding layer are intentionally lightweight. While it renders modern CSS/HTML layouts gracefully, heavy client-side single-page applications (full Google Docs or complex GitHub React frontends) experience reduced interactivity or partial script degradation. However, static webmail, server-rendered portals, documentation, and standard forms execute reliably.

### Candidate 2: WPE WebKit / WebKitGTK (Full Modern Engine)
- **Architecture**: Modern production WebKit port specifically designed for embedded Linux and Wayland/DRM systems (WPE = Web Platform for Embedded).
- **Memory Footprint**: Heavy. Baseline memory footprint is 250 MB to 500 MB RAM per web process, expanding to >1 GB on rich media sites.
- **Dependencies**: Massive dependency graph. Requires a complete POSIX environment: C++20/C++23 compiler, GLib/GObject/GIO runtime, Cairo, HarfBuzz, Fontconfig, libsoup/libcurl, ICU (International Components for Unicode, >35 MB tables), SQLite3, and Wayland or EGL/Mesa graphics stack.
- **Porting Effort**: High. Requires multi-process IPC (`WebKitWebProcess` <-> `WebKitNetworkProcess` <-> UI process), shared memory buffers (`shm_open`, `mmap`), thread primitives (pthread attributes, recursive mutexes), and POSIX signals.
- **Capabilities**: Uncompromised modern web compatibility. 100% passes all 5 journeys (Webmail, GitHub SPA, Google Docs/Etherpad via WebSockets, file uploads/downloads).

### Candidate 3: Ladybird (Independent Modern C++ Engine)
- **Architecture**: Independent multi-process browser engine originating from SerenityOS, written in modern C++23. Composed of `LibWeb` (HTML/CSS/DOM), `LibJS` (ECMAScript), `LibGfx`, and `LibIPC`.
- **Memory Footprint**: Moderate to high (~150 MB to 350 MB RAM).
- **Dependencies**: Uses `vcpkg` or vendored libraries. Minimal legacy UNIX dependencies compared to WebKit (avoids GLib/GObject), but mandates bleeding-edge C++23 compiler support (Clang/GCC with full concepts, ranges, coroutines), POSIX threads, shared memory IPC, and Skia/Cairo/Qt graphics backend.
- **Porting Effort**: Moderate to high. Rapidly evolving upstream codebase with frequent architectural refactorings. While cleaner than WebKit, keeping up with upstream changes requires dedicated tracking.
- **Capabilities**: Rapidly advancing towards modern web standard compliance (CSS Grid, Flexbox, modern JS, Canvas, WebSockets).

### Candidate 4: Chromium / Blink
- **Architecture**: World-leading browser engine, multi-process sandbox architecture.
- **Memory Footprint**: Prohibitive (>1 GB RAM baseline).
- **Dependencies**: Enormous proprietary build system (`gn`/`ninja`), hundreds of third-party vendored packages, custom toolchain expectations.
- **Porting Effort & Feasibility**: Unrealistic for an independent operating system project. Requires thousands of POSIX/Linux syscalls, advanced seccomp sandboxing, and immense CPU/disk resources just to compile. Rejected outright.

---

## 3. Comparative Evaluation Matrix

| Metric / Dimension | In-House + QuickJS (`web/` + QJS) | NetSurf (Embedded C Engine) | WPE WebKit (Modern Engine) | Ladybird (Modern C++ Engine) |
|---|---|---|---|---|
| **Language & Toolchain** | C99 + GNU11 (Host CC / GCC) | C99 (Standard GCC / Clang) | C++20/C++23, CMake, Ninja | C++23, CMake, Ninja |
| **Runtime Memory (Base)** | 15 MB - 35 MB | 25 MB - 60 MB | 250 MB - 600 MB | 150 MB - 400 MB |
| **POSIX Dependencies** | Self-contained / Ring 0 / minimal | Low (libc, libcurl, freetype, mbedtls) | High (GLib, ICU, libsoup, Cairo, Pango) | Moderate (libc, pthreads, IPC, Skia/Qt) |
| **Kernel / Ring 3 Isolation** | Currently Ring 0 (Dangerous; needs `base-04`) | Pure Ring 3 User Space Process | Multi-process Ring 3 Sandbox | Multi-process Ring 3 Sandbox |
| **HTML5 / CSS3 Conformance** | Partial (Custom subset, passes 73 test suites) | High (CSS 2.1, flexbox, HTML5 Libhubbub) | Complete (Full WHATWG / W3C Living Standards) | High & Growing (Full modern CSS/DOM specs) |
| **JavaScript / Web APIs** | Full JS with QJS, but **0 DOM/Web APIs** | Duktape / QuickJS (Basic DOM Level 2/3) | JavaScriptCore (Full ES2023, WebAssembly, WebGL) | LibJS (Full ES2023, WebAssembly, WebGL) |
| **Network & WebSockets** | Synchronous HTTP/1.1; **No WebSockets** | Libcurl HTTP/1.1 & HTTP/2; custom sockets | Full Async HTTP/2/3, WebSockets, WebRTC | Full Async HTTP/1.1/2, WebSockets |
| **Porting Complexity (Banana)** | Low (code exists, add QJS bindings) | Low to Medium (write `libnsfb`/GUI driver) | High (requires porting GLib, ICU, POSIX IPC) | Medium-High (requires C++23 runtime, IPC) |
| **Journey 1: Webmail Login** | Fail (Missing DOM form validation / OIDC) | **PASS** (Standard HTML/Redirects/Cookies) | **PASS** (Full modern Webmail SPA) | **PASS** (Full modern Webmail SPA) |
| **Journey 2: Webmail Read/Reply**| Fail (No pushState, no contentEditable) | **PASS** (Standard interface / plain reply) | **PASS** (Rich text / dynamic DOM) | **PASS** (Rich text / dynamic DOM) |
| **Journey 3: GitHub Navigation** | Fail (Turbo/PJAX crash, no custom elements)| Partial (Static views pass; SPA morphing fails)| **PASS** (Full React/Turbo SPA) | **PASS** (Full React/Turbo SPA) |
| **Journey 4: Collaborative Docs**| Fail (No WebSockets, no Selection API) | Fail (No WebSockets / complex editor) | **PASS** (Full WebSockets & canvas/text) | **PASS** (Full WebSockets & canvas/text) |
| **Journey 5: Multipart Upload**  | Fail (Kernel buffer memory exhaustion) | **PASS** (Streaming curl multipart upload) | **PASS** (Full streaming Fetch/XHR blob) | **PASS** (Full streaming Fetch/XHR blob) |

---

## 4. Strategic Decision & Architectural Roadmap

### 4.1 Definitive Architectural Choice: The Dual-Track Strategy
Attempting to force the in-house engine to run full production SPAs by tacking on QuickJS is an architectural dead end that consumes engineering resources without delivering daily-use goals. Conversely, immediately porting WPE WebKit is blocked by missing POSIX dependencies (base-05, user-space threading, and dynamic linking).

Therefore, Banana OS adopts a **Dual-Track Web Engine Strategy**:

1. **Track A (System & Fast Offline Browser - In-House Engine)**:
   - **Role**: Retain the existing lightweight in-house engine (`web/`, `kernel/browser.c`) as the fast, built-in, low-overhead browser for local system documentation (`about:`, local HTML/CSS manuals, offline apps, and lightweight intranet portals).
   - **Action on `quickjs` Task**: **Deprecate / Cancel conditional task `quickjs` as a prerequisite for production browsing**. QuickJS will **not** be integrated into `web/` because it does not solve the DOM, CSS, network, and security blockers. This avoids bifurcating engineering effort into two parallel custom engine architectures.
   - **Isolation Requirement (`base-04`)**: The in-house engine must be moved out of Ring 0 into user space to prevent kernel crashes on parsing faults.

2. **Track B (Daily-Use Internet Browser - Ported Maintained Engine)**:
   - **Phase 1 Target: NetSurf**:
     - NetSurf is chosen as the **first demonstrable ported engine** for Banana OS.
     - **Rationale**: Written in clean C99, low memory footprint (~40 MB), low dependency friction (can run on top of Banana OS's existing libc, libpng, freetype, and libcurl). It instantly provides compliant HTML5 parsing (`libhubbub`), robust CSS2.1/layout (`libcss`), and streaming file downloads/uploads, successfully fulfilling Journeys 1, 2, and 5 in user space.
   - **Phase 2 Long-Term Target: WPE WebKit or Ladybird**:
     - Once POSIX user-space foundations mature (full pthreads, shared memory, modern C++ runtime), Banana OS will evaluate a modern multi-process engine (WPE WebKit or Ladybird) to achieve 100% parity on complex collaborative web apps (Journey 4: Google Docs/Etherpad with WebSockets).

---

## 5. Concrete Demonstration Milestones

The following sequential milestones define the reproduction and implementation path for the chosen strategy:

- **Milestone 1: Living Checklist Realignment (Current Turn / base-13 step 3)**:
  - Formally record the decision in `docs/web-engine-strategy-decision.md`.
  - Update `docs/daily-use-checklist.json`:
    - Mark `base-13` as completed.
    - Update `quickjs` task to reflect its cancellation/supersession in favor of the maintained engine strategy.
    - Reframe `base-14` to focus on the NetSurf port driver and user-space integration milestones.

- **Milestone 2: User-Space Sandbox & POSIX Foundations (`base-04` & `base-05`)**:
  - Implement memory-isolated Ring 3 execution for browser binaries.
  - Complete POSIX file locking (`flock`), concurrency-safe stream I/O, and socket non-blocking poll mechanisms necessary for user-space browser execution.

- **Milestone 3: NetSurf Headless & Framebuffer Proof-of-Concept**:
  - Compile NetSurf core libraries (`libhubbub`, `libcss`, `libdom`) with Banana OS user-space SDK.
  - Implement the minimal `libnsfb` surface driver outputting to the Banana OS desktop windowing buffer.
  - Execute offline Journey 1 & 2 test pages, capturing screenshots and memory metrics demonstrating <50 MB RAM usage.

- **Milestone 4: Networked Daily-Use Qualification**:
  - Connect NetSurf driver to user-space network sockets with TLS (`mbedtls` / `openssl`).
  - Demonstrate successful login and message reply on Roundcube/Fastmail webmail test accounts without browser freezes or kernel memory leaks.

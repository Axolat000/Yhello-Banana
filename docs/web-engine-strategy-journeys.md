# Web Engine Strategy: Five User Journeys Specification

This specification defines the five concrete evaluation journeys (parcours) established for **base-13 step 1** ("Fixer cinq parcours précis : par exemple login messagerie, lire/répondre, dépôt GitHub, modifier un document et upload ; utiliser des comptes de test").

These journeys establish the exact benchmark against which the in-house browser engine (`web/`, `kernel/browser.c`) and candidate maintained engines (such as QuickJS integration vs. a full maintained engine port) are evaluated in steps 2 and 3.

---

## 1. Overview and Evaluation Rules

1. **Anti-superficiality rule**: A journey is **never** considered successful simply because a login screen or initial landing page rendered. Success requires completing every sequential action up to persistent remote verification.
2. **Deterministic & Reproducible**: Each journey defines target services, controlled test account conventions, exact interaction sequences, required technical subsystem boundaries (JavaScript runtime, DOM/events, network/HTTP, storage/cookies, rendering), and strict pass/fail criteria.
3. **Dual Target Architecture**: For each journey, both:
   - A **primary live / standard target** (real-world representative service), and
   - A **standard open reference instance / test fixture** (reproducible without proprietary CAPTCHA or external vendor lockout) are specified.

---

## 2. Journey Specifications

### Journey 1: Webmail Authentication (Login Session & Token Establishment)

- **Purpose**: Validate initial authentication negotiation, cookie jar persistence across redirects, TLS credential transmission, and auth token establishment.
- **Representative Target**: Fastmail / Roundcube Webmail (open-standards reference instance: self-hosted or public test Roundcube instance `https://roundcube.org/` demo or dedicated test deployment).
- **Secondary Target**: Gmail (standard Web interface or basic HTML mode `https://mail.google.com/mail/u/0/h/`).
- **Test Account Setup**:
  - Dedicated automated test account credentials (e.g., username `test-worker@...`, token/password provided via environment or test runner configuration; never hardcoded or credential-leaked).
- **Required User Actions**:
  1. Navigate to the login endpoint.
  2. Locate username and password `<input>` fields within the authentication `<form>`.
  3. Enter test credentials via synthetic or driver keyboard input events (`input`, `change`, `keydown`).
  4. Submit form (via `click` on submit button or form submit event).
  5. Follow HTTP redirects (302/303) and receive `Set-Cookie` directives (session cookies and `HttpOnly` auth cookies).
  6. Land on authenticated inbox view (`/inbox` or equivalent).
- **Technical Subsystems Exercised**:
  - **Network / HTTP**: HTTPS POST with `application/x-www-form-urlencoded` or JSON payload; multi-hop redirect handling preserving cookies.
  - **Storage / Cookies**: Cookie jar (`g_cookies`) handling `HttpOnly`, `SameSite`, `Secure`, and path/domain scoping.
  - **DOM / JS**: Form submission event handling, password field masking, DOM event dispatch.
- **Pass Criteria**:
  - Successful arrival at authenticated view; server-issued session cookie stored and present in subsequent request headers.
  - No uncaught JavaScript exceptions terminating page execution.
- **Failure Indicators**:
  - Stuck at login screen; silent redirect loop; dropped session cookies on redirect hop; form input values ignored or cleared without submission.

---

### Journey 2: Webmail Read & Reply (Interactive Messaging)

- **Purpose**: Validate dynamic view rendering, asynchronous content retrieval (fetch/XHR), rich/plain text area interaction, and message dispatch.
- **Representative Target**: Roundcube Webmail / Fastmail inbox & message composition.
- **Test Account Setup**:
  - Test account containing at least one pre-seeded message with a distinct subject (`Subject: BananaOS Evaluation Test <UUID>`).
- **Required User Actions**:
  1. From authenticated inbox, select and click on the target test email thread in the message list.
  2. Verify thread subject, sender, and body render legibly in the view area.
  3. Click "Reply" / "Answer" button.
  4. Ensure compose area receives focus and pre-fills quoted text or recipient address.
  5. Type response string (`Banana OS Automated Reply Verification <timestamp>`).
  6. Click "Send" button.
- **Technical Subsystems Exercised**:
  - **JavaScript / DOM**: Dynamic DOM tree updates (inserting compose node, updating message viewer without full page refresh), asynchronous `fetch` or `XMLHttpRequest`.
  - **Layout & Render**: Text area reflow, scrolling within message body container, TrueType font rendering of variable text.
  - **Network**: Asynchronous HTTP POST or REST API call with JSON/form data.
- **Pass Criteria**:
  - Message is dispatched with HTTP 200/204 response from mail server; response appears in Sent folder or mail spool confirmed via IMAP/HTTP test probe.
- **Failure Indicators**:
  - Message thread fails to expand; reply button unresponsive due to missing event listener or JS runtime error; compose area uneditable; reply request dropped.

---

### Journey 3: GitHub Repository Navigation & Commit Inspection

- **Purpose**: Validate modern single-page-application (SPA) navigation or Turbo/PJAX-driven DOM morphing, SVG icon rendering, and responsive table/tree layout.
- **Representative Target**: GitHub (`https://github.com/Axolat000/Yhello-Banana` or public repository `https://github.com/torvalds/linux`).
- **Test Account Setup**:
  - Public repository browsing (unauthenticated) and authenticated repository read access with test account.
- **Required User Actions**:
  1. Navigate to the repository root page (`/Axolat000/Yhello-Banana`).
  2. Verify file tree renders (file names, directory icons, latest commit header).
  3. Click on a file link (e.g. `README.md`).
  4. Observe URL update and file content view rendered (PJAX / History API `pushState` navigation).
  5. Navigate to the "Commits" tab / list (`/commits/main`).
  6. Click a specific commit SHA and inspect the unified/split diff view.
- **Technical Subsystems Exercised**:
  - **JavaScript**: HTML5 History API (`pushState`, `popstate`), Turbo/Stimulus or React SPA hydration, custom elements / web components (`<relative-time>`, `<tab-container>`).
  - **CSS / Layout**: Flexbox, CSS grid, CSS custom properties (variables), sticky table headers, monospace code styling.
  - **SVG**: Inline SVG rendering (`web/svg.c`) for tree icons and branch indicators.
- **Pass Criteria**:
  - File contents and commit diff view rendered accurately with readable code lines and commit metadata; browser navigation history records steps.
- **Failure Indicators**:
  - White screen on navigation (SPA router crash); unsupported custom element throwing fatal error; unstyled diff table; unresponsive links.

---

### Journey 4: Document Editing & Note Taking (Collaborative / Rich Document)

- **Purpose**: Validate complex DOM editing, `contentEditable` or virtualized document canvas/textarea, keyboard input dispatch, and real-time auto-saving.
- **Representative Target**: HedgeDoc / Etherpad / Nextcloud Text (standard Markdown/rich text collaborative editor).
- **Secondary Reference Target**: Google Docs / HackMD (view and edit mode).
- **Test Account Setup**:
  - Test instance or workspace with pre-created evaluation document accessible with edit token or test account.
- **Required User Actions**:
  1. Navigate to document URL (`/doc/banana-os-eval`).
  2. Wait for editor component initialization (Markdown pane or rich text buffer).
  3. Click inside editor buffer to gain focus.
  4. Append structured text: `# Heading 1\nBanana OS Document Test Line 1\n- Item A\n- Item B`.
  5. Trigger save (automatic timer debounce or explicit Ctrl+S / Save button).
  6. Reload page or open document in a second tab and verify that the appended content persists.
- **Technical Subsystems Exercised**:
  - **DOM**: `contentEditable` or multi-line `textarea` key event dispatch (`beforeinput`, `input`, `keyup`); selection and range API (`getSelection()`).
  - **Network / Transport**: WebSocket connection (`ws://` / `wss://`) or periodic HTTP REST delta synchronization (`PATCH` / `PUT`).
  - **Timers**: `setInterval`, `requestAnimationFrame`, or debounce timers in JS runtime.
- **Pass Criteria**:
  - Appended text is retained in document buffer, persisted to the server, and correctly re-loaded on page refresh.
- **Failure Indicators**:
  - Cursor cannot be placed in editor; keypresses do not alter document tree; save mechanism fails due to missing WebSocket or fetch implementation; unsaved changes modal blocks exit.

---

### Journey 5: File Upload & Download (Multipart Form & Binary Transfer)

- **Purpose**: Validate file picker interaction, `FormData` multipart/form-data encoding, binary stream upload, HTTP Content-Disposition download handling, and local storage persistence.
- **Representative Target**: Nextcloud / MinIO / HTTP File Drop reference test service (`/upload`).
- **Test Account Setup**:
  - Test user account with an allocated upload directory quota and known test file (`test-sample-4k.dat`, SHA-256 verified).
- **Required User Actions**:
  1. Navigate to upload interface (`/files`).
  2. Click upload button or trigger file selection input (`<input type="file">`).
  3. Select local test file path (via file chooser abstraction `kernel/filechooser.h`).
  4. Submit upload form or trigger automated upload via `FormData` and `XMLHttpRequest`/`fetch`.
  5. Monitor upload progress bar or confirmation status.
  6. Verify file listing displays uploaded file with correct size and timestamp.
  7. Click download link for the newly uploaded file; verify downloaded byte stream matches original SHA-256 hash.
- **Technical Subsystems Exercised**:
  - **Kernel / GUI**: Native file chooser integration (`kernel/filechooser.h`, `kernel/browser.c`).
  - **DOM / JS**: `File`, `FileList`, `Blob`, `FormData` API objects; `FileReader` or direct stream passing.
  - **Network / HTTP**: HTTP `POST` multipart/form-data stream serialization; HTTP `GET` with binary octet-stream handling and download to disk.
- **Pass Criteria**:
  - Uploaded file byte count and checksum on remote server match source file exactly; download retrieves exact identical byte stream.
- **Failure Indicators**:
  - Inability to bind local file descriptor to browser DOM file input; corrupted multipart boundaries; memory allocation failure on multi-megabyte payloads; dropped download headers.

---

## 3. Journey Evaluation Matrix Template (for base-13 Steps 2 & 3)

| Journey | Step Description | In-House Engine Status | Blocker Subsystem | QuickJS Impact | Full Engine Port Feasibility |
|---|---|---|---|---|---|
| **J1: Webmail Login** | 1.1 Render Login Form | Untested / Partial | HTML / CSS | None (layout engine needed) | High (Blink/WebKit/Ladybird) |
| | 1.2 Form Input & Submit | Partial | DOM Events / Form serialization | Partial | High |
| | 1.3 Cookie & Redirect | Implemented | Cookie jar / HTTP redirect | None (HTTP subsystem) | High |
| | 1.4 Auth Inbox Render | Blocked | JS SPA / Web APIs | Low (needs DOM/CSS) | High |
| **J2: Webmail Read/Reply**| 2.1 Thread Listing Fetch | Blocked | Fetch / CORS / Promises | Medium (QuickJS has ES6) | High |
| | 2.2 Compose & Edit | Blocked | ContentEditable / Selection | Low | High |
| | 2.3 Send Message (POST) | Blocked | HTTP client / REST | Low | High |
| **J3: GitHub Browsing** | 3.1 Repo Tree Render | Partial | CSS Grid / Flex / Custom Elements | Low | High |
| | 3.2 SPA / PJAX Navigation| Blocked | History API / pushState | Low | High |
| | 3.3 Commit Diff View | Partial | Monospace wrap / SVG | Low | High |
| **J4: Document Edit** | 4.1 Document Load | Blocked | WebSockets / Canvas / JS | Medium | High |
| | 4.2 Interactive Editing | Blocked | Range / Selection / Key events | Low | High |
| | 4.3 Persistence / Sync | Blocked | WebSocket / Async I/O | Low | High |
| **J5: Upload / Download** | 5.1 File Picker Hookup | Partial | FileChooser / Input binding | None | High |
| | 5.2 Multipart Upload | Blocked | FormData / Stream POST | Low | High |
| | 5.3 Binary Download | Partial | Content-Disposition / FS write | None | High |

---

## 4. Execution & Validation Plan

1. **Step 2 Qualification**: Using the 5 journeys above, catalog exact missing APIs, HTTP constraints, and runtime exceptions when pointing the browser or test harnesses to offline or controlled fixtures of these services.
2. **Step 3 Trade-Off Assessment**:
   - **QuickJS**: Provides ES2020 JavaScript engine, but provides **zero** HTML parser, CSS engine, DOM implementation, or layout/rendering tree.
   - **Full Maintained Engine (e.g. Ladybird, NetSurf, Chromium/Blink)**: Provides full web stack, but introduces heavy libc/POSIX dependencies, C++ runtime, threading models, and GPU acceleration requirements.

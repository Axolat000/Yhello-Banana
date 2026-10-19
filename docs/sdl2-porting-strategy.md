# Port SDL sur Banana OS : Architecture, Choix de Version et Programme Cible

Ce document formalise la réalisation de la **sous-étape 1** du jalon `sdl` (« Choisir un premier programme cible et la version SDL qu’il utilise ; créer le port correspondant au lieu de supposer SDL2/SDL3 interchangeables »).

---

## 1. Analyse Comparative et Choix de Version : SDL2 vs SDL3 vs SDL 1.2

### 1.1 Non-interchangeabilité des versions majeures
- **SDL 1.2** : API historique (1998–2012) basée sur une seule surface d'affichage globale (`SDL_SetVideoMode`, surfaces 8/16/32-bit directes). Elle ne supporte ni multi-fenêtrage natif moderne, ni redimensionnement dynamique propre, et son écosystème est obsolète ou rétro-porté via `sdl12-compat`.
- **SDL 3** : Nouvelle mouture architecturale (en cours de stabilisation). L'API a été profondément remaniée (`SDL_Window`, `SDL_Renderer`, suppression de pans entiers de compatibilité directe avec les jeux existants, refonte des sous-systèmes d'événements et de propriétés). Presque aucun jeu classique ou émulateur prêt à l'emploi n'est nativement écrit en SDL3 aujourd'hui.
- **SDL2** : Standard de facto de l'industrie pour les jeux et applications multimédias multiplateformes depuis plus de 10 ans. L'immense majorité des applications cibles candidates (ports de Doom comme Chocolate Doom ou PrBoom+, ScummVM, RetroArch/emulateurs, ioquake3, jeux indie) ciblent SDL2. SDL2 gère explicitement le fenêtrage moderne (`SDL_Window`), le rendu logiciel (`SDL_Surface` / `SDL_UpdateWindowSurface`), la capture souris relative (`SDL_SetRelativeMouseMode`), l'audio en callback PCM (`SDL_OpenAudio`), et la couche OpenGL (`SDL_GL_CreateContext`).

### 1.2 Décision de Version
**Choix retenu : SDL2 (branche stable 2.30.x)**.
Ce choix garantit :
1. L'adéquation immédiate avec le premier programme cible sélectionné sans nécessiter de couche de traduction supplémentaire.
2. Une base de référence documentée pour l'implémentation de la sous-étape 2 (backends vidéo, surface logicielle, événements, timer, threads et audio).
3. La préservation de l'évolution future : SDL3 propose une couche `sdl2-compat` si nécessaire, mais le portage natif d'un backend SDL2 fournit la surface d'application la plus large et la plus testable sur Banana OS.

---

## 2. Choix du Premier Programme Cible

### 2.1 Critères d'évaluation
Pour qualifier le port selon l'objectif de la checklist (« Une application multimédia et un jeu ciblé fonctionnent avec son, entrées, redimensionnement et fermeture sans fuite ») :
- Le programme doit être autonome, auditable, et compilable avec la libc du SDK Banana OS (`sdk/include`, `sdk/lib`).
- Il doit solliciter :
  1. Le cycle de fenêtrage et surface logicielle (rendu frame par frame sans GPU obligatoire).
  2. Les entrées clavier et souris (y compris capture relative pour la visée et gestion des touches directionnelles/action).
  3. Le timer monotone et la boucle d'événements cadencée (60/35 FPS).
  4. L'audio PCM en continu (effets sonores et musique).
  5. Une fermeture propre avec libération de toutes les ressources (0 fuite mémoire).

### 2.2 Programme Sélectionné : Chocolate Doom (Doom Classic Engine)
- **Programme** : **Chocolate Doom** (moteur Doom classique ciblant SDL2).
- **Justification technique** :
  - **Graphismes** : Moteur logiciel 320x200 étiré/scalé vers la surface de fenêtre Banana OS (`SDL_UpdateWindowSurface`), idéal pour valider le chemin logiciel avant OpenGL.
  - **Entrées** : Clavier complet (ZQSD/AZERTY, flèches, tir, utilisation) et souris avec mode capture relatif (`SDL_SetRelativeMouseMode`), parfaitement aligné avec `bwin_capture_mouse` / `BANANA_EV_MOUSE_REL` introduit dans l'ABI Banana v20.
  - **Audio** : Moteur son callback PCM 11025/22050/44100 Hz 16-bit stéréo, se branchant directement sur le sous-système audio Banana (`audio_play`, `audio_queued_ms`).
  - **Durabilité / Tests** : Exécutable en mode démo automatisé (timerefresh / demo playback 30 minutes sans blocage de touches ni résidu sonore).
- **Programme de qualification intermédiaire / démo** :
  - **`ports/sdl2/test/sdl2_demo.c`** : Une application de démonstration interactive et de stress test automatisé permettant de tester unitairement :
    - Ouverture, dessin logiciel, redimensionnement dynamique (`BANANA_EV_RESIZE`).
    - Capture/relâchement de souris et frappe clavier.
    - Émission de tonalités/formes d'ondes audio PCM.
    - Test de fermeture répétée (100 cycles d'ouverture/fermeture sans fuite) conformément aux critères de test de la checklist.

---

## 3. Matrice de Correspondance Technique (Banana OS ABI v22 vs SDL2)

| Sous-système SDL2 | Primitives SDL2 requises | Interface Banana OS SDK / Kernel | Remarques d'implémentation |
| :--- | :--- | :--- | :--- |
| **Vidéo & Fenêtre** | `SDL_CreateWindow`, `SDL_DestroyWindow`, `SDL_SetWindowTitle`, `SDL_SetWindowSize` | `bwin_open`, `bwin_close`, `bwin_title`, `bwin_resizable` | Fenêtre gérée par `kernel/appwin.c`. Mapping 1-to-1 du descripteur de fenêtre. |
| **Surface Logicielle** | `SDL_GetWindowSurface`, `SDL_UpdateWindowSurface`, `SDL_UpdateWindowSurfaceRects` | `win->px`, `bwin_update` | Buffer pixel ARGB 0x00RRGGBB direct. Redimensionnement réalloue `px` sur `BANANA_EV_RESIZE`. |
| **Événements** | `SDL_PollEvent`, `SDL_PumpEvents`, `SDL_WaitEventTimeout` | `bwin_event`, `bwin_wait_event` | Traduction de `banana_event_t` vers `SDL_Event` (`SDL_KEYDOWN`, `SDL_KEYUP`, `SDL_MOUSEBUTTONDOWN`, `SDL_MOUSEMOTION`, `SDL_WINDOWEVENT`). |
| **Souris / Capture** | `SDL_SetRelativeMouseMode`, `SDL_GetRelativeMouseState` | `bwin_capture_mouse`, `BANANA_EV_MOUSE_REL` | ABI v20 : delta dx/dy transmis via les événements relatifs. |
| **Temps & Timers** | `SDL_GetTicks`, `SDL_GetTicks64`, `SDL_Delay`, `SDL_GetPerformanceCounter` | `banana_ticks()`, `banana_sleep()` | Horloge monotone matérielle du timer noyau (`kernel/timer.c`, `ticks_ms`). |
| **Threads & Mutex** | `SDL_CreateThread`, `SDL_WaitThread`, `SDL_CreateMutex`, `SDL_LockMutex`, `SDL_UnlockMutex` | `sdk/lib/pthread.c` (`pthread_create`, `pthread_join`, `pthread_mutex_*`) | Support natif complet POSIX threads déjà intégré dans le SDK Banana. |
| **Audio** | `SDL_OpenAudio`, `SDL_PauseAudio`, `SDL_CloseAudio` | `audio_play`, `audio_stop`, `audio_queued_ms` | Thread de mixage alimentant le buffer callback et transmettant les blocs PCM au driver noyau via `banana_api->audio_play`. |
| **OpenGL (étape 3)** | `SDL_GL_CreateContext`, `SDL_GL_MakeCurrent`, `SDL_GL_SwapWindow` | `sdk/lib/gl3.c` (`bgl3_create`, `bgl3_swap`, OSMesa 3.3) | Activation conditionnelle après validation complète du pipeline logiciel. |

---

## 4. Architecture du Port dans le Dépôt

Le portage est structuré selon les conventions établies dans `ports/` (`ports/ffmpeg`, `ports/lwjgl`, `ports/openjdk`) :
```
ports/sdl2/
├── Makefile             # Construction de la bibliothèque libSDL2.a
├── include/
│   └── SDL2/            # En-têtes publics SDL standardisés (SDL.h, SDL_video.h, SDL_events.h, ...)
├── src/
│   ├── video/banana/    # Backend vidéo Banana OS (bwin, surface, curseur)
│   ├── events/banana/   # Traduction des événements Banana OS -> SDL_Event
│   ├── timer/banana/    # SDL_GetTicks mappé sur banana_ticks
│   ├── audio/banana/    # Backend audio Banana OS (audio_play)
│   └── thread/pthread/  # Sous-système threads mappé sur le pthread Banana
└── test/
    ├── Makefile         # Makefile pour les programmes de test et qualification
    └── sdl2_demo.c      # Démo interactive et test de résistance (100 cycles)
```

Ce document et l'arborescence initiale fixent le socle de spécification et d'ingénierie requis pour la mise en œuvre de la sous-étape 2.

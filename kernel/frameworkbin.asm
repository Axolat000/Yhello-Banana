; Pinned framework fixtures, shared by the browser demo and its real tests.
section .rodata
%macro FIXTURE 2
align 16
global framework_%1, framework_%1_end
framework_%1: incbin %2
framework_%1_end:
%endmacro
FIXTURE react, "web/fixtures/react.js"
FIXTURE reactdom, "web/fixtures/react-dom.js"
FIXTURE vue, "web/fixtures/vue.js"
FIXTURE alpine, "web/fixtures/alpine.js"
FIXTURE tailwind, "web/fixtures/tailwind.css"
FIXTURE reacthtml, "web/fixtures/react.html"
FIXTURE vuehtml, "web/fixtures/vue.html"
FIXTURE alpinehtml, "web/fixtures/alpine.html"
FIXTURE licenses, "web/fixtures/licenses.html"
%include "web/fixtures/astro_resources.asm"
FIXTURE mutations, "web/test/cases/mutation_observer.html"
FIXTURE mutations_async, "web/test/cases/mutation_observer_async.html"
FIXTURE mutations_limits, "web/test/cases/mutation_observer_limits.html"
section .note.GNU-stack noalloc noexec nowrite progbits

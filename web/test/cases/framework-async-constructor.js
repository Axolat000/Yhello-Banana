function equal(actual, expected) {
    if (actual !== expected) throw new Error(actual + " !== " + expected);
}
const AsyncFunction = Object.getPrototypeOf(async function(){}).constructor;
equal(AsyncFunction.name, 'AsyncFunction');
equal(Object.getPrototypeOf(function(){}), Function.prototype);
equal(Object.getPrototypeOf(async function(){}), AsyncFunction.prototype);
equal(Object.getPrototypeOf(async () => 1), AsyncFunction.prototype);
equal(Object.getPrototypeOf(AsyncFunction.prototype), Function.prototype);
equal((async function(){}).constructor, AsyncFunction);
equal(typeof (async function(){}).prototype, 'undefined');
equal(typeof AsyncFunction.prototype.call, 'function');
equal(Function('a', 'return a + 1')(4), 5);
equal(Function('return 7')(), 7);
const made = new AsyncFunction('a', 'b', 'return await Promise.resolve(a + b)');
equal(made.constructor, AsyncFunction);
equal(Object.getPrototypeOf(made), AsyncFunction.prototype);
equal(made(2, 3) instanceof Promise, true);
made(2, 3).then(v => equal(v, 5));
AsyncFunction('return this.value').call({value: 9}).then(v => equal(v, 9));
let globalValue = 17;
(function() {
    let globalValue = 99;
    AsyncFunction('return globalValue')().then(v => equal(v, 17));
})();
AsyncFunction('throw new Error("rejected")')().catch(e => equal(e.message, 'rejected'));
let syntaxFailed = false;
try { AsyncFunction('return )'); } catch (e) { syntaxFailed = e.name === 'SyntaxError'; }
equal(syntaxFailed, true);
let newFailed = false;
try { new made(); } catch (e) { newFailed = e.name === 'TypeError'; }
equal(newFailed, true);
console.log('framework-async-constructor: PASS');

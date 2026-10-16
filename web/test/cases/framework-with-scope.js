function equal(actual, expected) {
    if (actual !== expected) throw new Error(actual + " !== " + expected);
}
let count = 90;
let outside = 11;
const scope = {count: 0, undef: undefined, method() { return this.count; }};
with (scope) {
    equal(count, 0);
    equal(outside, 11);
    equal(typeof undef, 'undefined');
    equal(typeof absent, 'undefined');
    count++;
    count += 2;
    equal(method(), 3);
    { let count = 4; equal(count, 4); }
    equal(count, 3);
}
equal(count, 90);
equal(scope.count, 3);
let closure;
with (scope) { closure = () => count++; }
closure();
equal(scope.count, 4);
scope[Symbol.unscopables] = {count: true};
with (scope) { equal(count, 90); }
delete scope[Symbol.unscopables];
with ({outer: 2}) { with ({inner: 3}) { equal(outer + inner, 5); } }
const state = {count: 5};
const traps = [];
const proxy = new Proxy(state, {
    has(target, key) { traps.push('has:' + key); return key in target; },
    get(target, key) { traps.push('get:' + key); return target[key]; },
    set(target, key, value) { traps.push('set:' + key); target[key] = value; return true; }
});
with (proxy) { count++; }
equal(state.count, 6);
equal(traps.includes('has:count'), true);
equal(traps.includes('get:count'), true);
equal(traps.includes('set:count'), true);
const AsyncFunction = Object.getPrototypeOf(async function(){}).constructor;
const evaluate = new AsyncFunction(['__self', 'scope'], 'with (scope) { __self.result = count++ }; __self.finished = true; return __self.result;');
const result = {};
evaluate(result, proxy).then(v => equal(v, 6));
equal(result.result, 6);
equal(result.finished, true);
equal(state.count, 7);
let caught = false;
try { with (null) {} } catch (e) { caught = e.name === 'TypeError'; }
equal(caught, true);
with (scope) { try { throw new Error('scope exit'); } catch (e) { equal(e.message, 'scope exit'); } }
equal(count, 90);
console.log('framework-with-scope: PASS');

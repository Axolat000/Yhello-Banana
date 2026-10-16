function equal(actual, expected) {
    if (actual !== expected) throw new Error(actual + " !== " + expected);
}
const target = {count: 0, empty: undefined};
let reads = 0;
Object.defineProperty(target, 'value', {get() { reads++; return 7; }, enumerable: true});
const proxy = new Proxy(target, {});
equal(Object.getOwnPropertyDescriptor(proxy, 'count').value, 0);
equal(Object.getOwnPropertyDescriptor(proxy, 'empty').value, undefined);
equal(Object.getOwnPropertyDescriptor(proxy, 'missing'), undefined);
equal(Object.prototype.hasOwnProperty.call(proxy, 'count'), true);
equal(Object.prototype.hasOwnProperty.call(proxy, 'empty'), true);
equal(Object.prototype.hasOwnProperty.call(proxy, 'missing'), false);
equal(Object.prototype.hasOwnProperty.call(proxy, 'value'), true);
equal(typeof Object.getOwnPropertyDescriptor(proxy, 'value').get, 'function');
equal(reads, 0);
const descriptors = {};
Object.getOwnPropertyNames(proxy).forEach(key => { descriptors[key] = Object.getOwnPropertyDescriptor(proxy, key); });
Object.entries(descriptors).forEach(([key, {value, enumerable}]) => { equal(enumerable, true); });
equal(reads, 0);
const nested = new Proxy(proxy, {});
equal(Object.getOwnPropertyDescriptor(nested, 'count').value, 0);
let trapped = false;
const custom = new Proxy(target, {getOwnPropertyDescriptor(object, key) {
    equal(object, target); equal(key, 'count'); trapped = true;
    return {value: 42, enumerable: true, configurable: true};
}});
equal(Object.getOwnPropertyDescriptor(custom, 'count').value, 42);
equal(trapped, true);
const invalid = new Proxy(target, {getOwnPropertyDescriptor() { return 3; }});
let threw = false;
try { Object.getOwnPropertyDescriptor(invalid, 'count'); } catch (e) { threw = e.name === 'TypeError'; }
equal(threw, true);
console.log('framework-proxy-descriptor: PASS');

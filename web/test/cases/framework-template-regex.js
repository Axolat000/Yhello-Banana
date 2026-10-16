// Run with the host script_test driver; each assertion must throw on failure.
function equal(actual, expected) {
    if (actual !== expected) throw new Error(actual + " !== " + expected);
}
const quoted = 'a"b"';
equal(`<div foo="${quoted.replace(/"/g,"&quot;")}"></div>`, '<div foo="a&quot;b&quot;"></div>');
equal(`${/['"`{}]/g.test('}')}`, 'true');
equal(`${/[/}]/.test('/')}`, 'true');
equal(`${/\//.test('/')}`, 'true');
equal(`${/\}/.test('}')}`, 'true');
equal(`${true ? /"/.test('"') : false}`, 'true');
equal(`${(() => { return /}/.test('}'); })()}`, 'true');
equal(`${({value: /}/}).value.test('}')}`, 'true');
equal(`${12 / 3 / 2}`, '2');
equal(`${(12) / 3}`, '4');
equal(`${[12][0] / 3}`, '4');
equal(`${'12' / 3}`, '4');
equal(`${`12` / 3}`, '4');
equal(`${({in: 12}).in / 3}`, '4');
let counter = 12;
equal(`${counter++ / 3}`, '4');
equal(`${24 / /x/.test('x') / 2}`, '12');
equal(`${1e+2 / 25}`, '4');
equal(`${2 /* } ` " / */ + 3}`, '5');
equal(`${2 // } ` " / ${ignored}
    + 3}`, '5');
equal(`outer ${`inner ${quoted.replace(/"/g, '_')} ${`${8 / 2}`}`}`, 'outer inner a_b_ 4');
function tag(parts, value) { return parts[0] + value + parts[1]; }
equal(tag`[${'}'.replace(/}/g, ']')}]`, '[]]');
equal(`${tag`[${/"/.test('"')}]`}`, '[true]');
console.log('framework-template-regex: PASS');

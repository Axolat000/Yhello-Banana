function equal(actual, expected) {
    if (actual !== expected) throw new Error(actual + " !== " + expected);
}
const any = /:([^]+)/;
equal(any.exec(':a\nb')[1], 'a\nb');
equal(any.source, ':([^]+)');
equal(new RegExp('[^]+').exec('\n\rtext')[0], '\n\rtext');
equal(/[^][^]/.test('\n\r'), true);
equal(/[^]/.test(''), false);
equal(/\[\^\]/.test('[^]'), true);
equal(/[\[^]/.test('['), true);
equal(/[^]/g.source, '[^]');
equal(`x${/[^]/.test('}')}y`, 'xtruey');
console.log('framework-regexp-any: PASS');

// |reftest| skip -- class-methods-private,class-fields-private,class-fields-public is not supported
// This file was procedurally generated from the following sources:
// - src/class-elements/rs-private-method-alt.case
// - src/class-elements/productions/cls-expr-after-same-line-async-gen.template
/*---
description: Valid PrivateName as private method (field definitions after an async generator in the same line)
esid: prod-FieldDefinition
features: [class-methods-private, class-fields-private, class, class-fields-public, async-iteration]
flags: [generated, async]
includes: [propertyHelper.js]
info: |
    ClassElement :
      MethodDefinition
      ...
      ;

    MethodDefinition :
      ClassElementName ( UniqueFormalParameters ) { FunctionBody }
      ...

    ClassElementName :
      PropertyName
      PrivateName

    PrivateName ::
      # IdentifierName

    IdentifierName ::
      IdentifierStart
      IdentifierName IdentifierPart

    IdentifierStart ::
      UnicodeIDStart
      $
      _
      \ UnicodeEscapeSequence

    IdentifierPart::
      UnicodeIDContinue
      $
      \ UnicodeEscapeSequence
      <ZWNJ> <ZWJ>

    UnicodeIDStart::
      any Unicode code point with the Unicode property "ID_Start"

    UnicodeIDContinue::
      any Unicode code point with the Unicode property "ID_Continue"

    NOTE 3
    The sets of code points with Unicode properties "ID_Start" and
    "ID_Continue" include, respectively, the code points with Unicode
    properties "Other_ID_Start" and "Other_ID_Continue".

---*/


var C = class {
  async *m() { return 42; } #$_; #__; #\u{6F}_; #℘_; #ZW_‌_NJ_; #ZW_‍_J_;
  #$() {
    return this.#$_;
  }
  #_() {
    return this.#__;
  }
  #\u{6F}() {
    return this.#\u{6F}_;
  }
  #℘() {
    return this.#℘_;
  }
  #ZW_‌_NJ() {
    return this.#ZW_‌_NJ_;
  }
  #ZW_‍_J() {
    return this.#ZW_‍_J_;
  }
;
  $(value) {
    this.#$_ = value;
    return this.#$();
  }
  _(value) {
    this.#__ = value;
    return this.#_();
  }
  \u{6F}(value) {
    this.#\u{6F}_ = value;
    return this.#\u{6F}();
  }
  ℘(value) {
    this.#℘_ = value;
    return this.#℘();
  }
  ZW_‌_NJ(value) {
    this.#ZW_‌_NJ_ = value;
    return this.#ZW_‌_NJ();
  }
  ZW_‍_J(value) {
    this.#ZW_‍_J_ = value;
    return this.#ZW_‍_J();
  }

}

var c = new C();

assert.sameValue(Object.hasOwnProperty.call(c, "m"), false);
assert.sameValue(c.m, C.prototype.m);

verifyProperty(C.prototype, "m", {
  enumerable: false,
  configurable: true,
  writable: true,
}, {restore: true});

c.m().next().then(function(v) {
  assert.sameValue(v.value, 42);
  assert.sameValue(v.done, true);

  function assertions() {
    // Cover $DONE handler for async cases.
    function $DONE(error) {
      if (error) {
        throw new Test262Error('Test262:AsyncTestFailure')
      }
    }
    assert.sameValue(c.$(1), 1);
    assert.sameValue(c._(1), 1);
    assert.sameValue(c.\u{6F}(1), 1);
    assert.sameValue(c.℘(1), 1);
    assert.sameValue(c.ZW_‌_NJ(1), 1);
    assert.sameValue(c.ZW_‍_J(1), 1);
  }

  return Promise.resolve(assertions());
}, $DONE).then($DONE, $DONE);

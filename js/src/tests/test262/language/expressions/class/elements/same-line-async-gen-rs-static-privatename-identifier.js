// |reftest| skip -- class-static-fields-private,class-fields-public is not supported
// This file was procedurally generated from the following sources:
// - src/class-elements/rs-static-privatename-identifier.case
// - src/class-elements/productions/cls-expr-after-same-line-async-gen.template
/*---
description: Valid Static PrivateName (field definitions after an async generator in the same line)
esid: prod-FieldDefinition
features: [class-static-fields-private, class, class-fields-public, async-iteration]
flags: [generated, async]
includes: [propertyHelper.js]
info: |
    ClassElement :
      MethodDefinition
      static MethodDefinition
      FieldDefinition ;
      static FieldDefinition ;
      ;

    FieldDefinition :
      ClassElementName Initializer _opt

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
  async *m() { return 42; } static #$; static #_; static #\u{6F}; static #\u2118; static #ZW_\u200C_NJ; static #ZW_\u200D_J;
  static $(value) {
    this.#$ = value;
    return this.#$;
  }
  static _(value) {
    this.#_ = value;
    return this.#_;
  }
  static \u{6F}(value) {
    this.#\u{6F} = value;
    return this.#\u{6F};
  }
  static \u2118(value) {
    this.#\u2118 = value;
    return this.#\u2118;
  }
  static ZW_\u200C_NJ(value) {
    this.#ZW_\u200C_NJ = value;
    return this.#ZW_\u200C_NJ;
  }
  static ZW_\u200D_J(value) {
    this.#ZW_\u200D_J = value;
    return this.#ZW_\u200D_J;
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
    assert.sameValue(C.$(1), 1);
    assert.sameValue(C._(1), 1);
    assert.sameValue(C.\u{6F}(1), 1);
    assert.sameValue(C.\u2118(1), 1);
    assert.sameValue(C.ZW_\u200C_NJ(1), 1);
    assert.sameValue(C.ZW_\u200D_J(1), 1);

  }

  return Promise.resolve(assertions());
}, $DONE).then($DONE, $DONE);

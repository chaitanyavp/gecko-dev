// |reftest| skip -- class-methods-private,class-fields-private,class-fields-public is not supported
// This file was procedurally generated from the following sources:
// - src/class-elements/rs-private-setter.case
// - src/class-elements/productions/cls-decl-after-same-line-async-method.template
/*---
description: Valid PrivateName as private setter (field definitions after an async method in the same line)
esid: prod-FieldDefinition
features: [class-methods-private, class-fields-private, class, class-fields-public, async-functions]
flags: [generated, async]
includes: [propertyHelper.js]
info: |
    ClassElement :
      MethodDefinition
      ...
      ;

    MethodDefinition :
      ...
      set ClassElementName ( PropertySetParameterList ) { FunctionBody }

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


class C {
  async m() { return 42; } #$_; #__; #\u{6F}_; #\u2118_; #ZW_\u200C_NJ_; #ZW_\u200D_J_;
  set #$(value) {
    this.#$_ = value;
  }
  set #_(value) {
    this.#__ = value;
  }
  set #\u{6F}(value) {
    this.#\u{6F}_ = value;
  }
  set #\u2118(value) {
    this.#\u2118_ = value;
  }
  set #ZW_\u200C_NJ(value) {
    this.#ZW_\u200C_NJ_ = value;
  }
  set #ZW_\u200D_J(value) {
    this.#ZW_\u200D_J_ = value;
  }
;
  $(value) {
    this.#$ = value;
    return this.#$_;
  }
  _(value) {
    this.#_ = value;
    return this.#__;
  }
  \u{6F}(value) {
    this.#\u{6F} = value;
    return this.#\u{6F}_;
  }
  \u2118(value) {
    this.#\u2118 = value;
    return this.#\u2118_;
  }
  ZW_\u200C_NJ(value) {
    this.#ZW_\u200C_NJ = value;
    return this.#ZW_\u200C_NJ_;
  }
  ZW_\u200D_J(value) {
    this.#ZW_\u200D_J = value;
    return this.#ZW_\u200D_J_;
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

c.m().then(function(v) {
  assert.sameValue(v, 42);

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
    assert.sameValue(c.\u2118(1), 1);
    assert.sameValue(c.ZW_\u200C_NJ(1), 1);
    assert.sameValue(c.ZW_\u200D_J(1), 1);
  }

  return Promise.resolve(assertions());
}, $DONE).then($DONE, $DONE);

// |reftest| skip -- class-static-methods-private,class-fields-public is not supported
// This file was procedurally generated from the following sources:
// - src/class-elements/rs-static-async-method-privatename-identifier-alt.case
// - src/class-elements/productions/cls-expr-after-same-line-static-async-method.template
/*---
description: Valid Static AsyncMethod PrivateName (field definitions after a static async method in the same line)
esid: prod-FieldDefinition
features: [class-static-methods-private, class, class-fields-public, async-functions]
flags: [generated, async]
includes: [propertyHelper.js]
info: |
    ClassElement :
      MethodDefinition
      static MethodDefinition
      FieldDefinition ;
      static FieldDefinition ;
      ;

    MethodDefinition :
      AsyncMethod

    AsyncMethod :
      async [no LineTerminator here] ClassElementName ( UniqueFormalParameters ){ AsyncFunctionBody }

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
  static async m() { return 42; } static async #$(value) {
    return await value;
  }
  static async #_(value) {
    return await value;
  }
  static async #o(value) {
    return await value;
  }
  static async #℘(value) {
    return await value;
  }
  static async #ZW_‌_NJ(value) {
    return await value;
  }
  static async #ZW_‍_J(value) {
    return await value;
  };
  static async $(value) {
    return await this.#$(value);
  }
  static async _(value) {
    return await this.#_(value);
  }
  static async o(value) {
    return await this.#o(value);
  }
  static async ℘(value) { // DO NOT CHANGE THE NAME OF THIS FIELD
    return await this.#℘(value);
  }
  static async ZW_‌_NJ(value) { // DO NOT CHANGE THE NAME OF THIS FIELD
    return await this.#ZW_‌_NJ(value);
  }
  static async ZW_‍_J(value) { // DO NOT CHANGE THE NAME OF THIS FIELD
    return await this.#ZW_‍_J(value);
  }

}

var c = new C();

assert.sameValue(Object.hasOwnProperty.call(c, "m"), false);
assert.sameValue(Object.hasOwnProperty.call(C.prototype, "m"), false);

verifyProperty(C, "m", {
  enumerable: false,
  configurable: true,
  writable: true,
}, {restore: true});

C.m().then(function(v) {
  assert.sameValue(v, 42);

  function assertions() {
    // Cover $DONE handler for async cases.
    function $DONE(error) {
      if (error) {
        throw new Test262Error('Test262:AsyncTestFailure')
      }
    }
    Promise.all([
      C.$(1),
      C._(1),
      C.o(1),
      C.℘(1), // DO NOT CHANGE THE NAME OF THIS FIELD
      C.ZW_‌_NJ(1), // DO NOT CHANGE THE NAME OF THIS FIELD
      C.ZW_‍_J(1), // DO NOT CHANGE THE NAME OF THIS FIELD
    ]).then(results => {

      assert.sameValue(results[0], 1);
      assert.sameValue(results[1], 1);
      assert.sameValue(results[2], 1);
      assert.sameValue(results[3], 1);
      assert.sameValue(results[4], 1);
      assert.sameValue(results[5], 1);

    }, $DONE).then($DONE, $DONE);
  }

  return Promise.resolve(assertions());
}, $DONE).then($DONE, $DONE);

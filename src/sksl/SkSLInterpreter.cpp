/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SKSL_STANDALONE

#include "src/core/SkRasterPipeline.h"
#include "src/sksl/SkSLInterpreter.h"
#include "src/sksl/ir/SkSLBinaryExpression.h"
#include "src/sksl/ir/SkSLExpressionStatement.h"
#include "src/sksl/ir/SkSLForStatement.h"
#include "src/sksl/ir/SkSLFunctionCall.h"
#include "src/sksl/ir/SkSLFunctionReference.h"
#include "src/sksl/ir/SkSLIfStatement.h"
#include "src/sksl/ir/SkSLIndexExpression.h"
#include "src/sksl/ir/SkSLPostfixExpression.h"
#include "src/sksl/ir/SkSLPrefixExpression.h"
#include "src/sksl/ir/SkSLProgram.h"
#include "src/sksl/ir/SkSLStatement.h"
#include "src/sksl/ir/SkSLTernaryExpression.h"
#include "src/sksl/ir/SkSLVarDeclarations.h"
#include "src/sksl/ir/SkSLVarDeclarationsStatement.h"
#include "src/sksl/ir/SkSLVariableReference.h"

namespace SkSL {

static constexpr int UNINITIALIZED = 0xDEADBEEF;

Interpreter::Value* Interpreter::run(const ByteCodeFunction& f, Interpreter::Value args[],
                                    Interpreter::Value inputs[]) {
    fCurrentFunction = &f;
    fStack.clear();
    fGlobals.clear();
#ifdef TRACE
    this->disassemble(f);
#endif
    for (int i = 0; i < f.fParameterCount; ++i) {
        this->push(args[i]);
    }
    for (int i = 0; i < f.fLocalCount; ++i) {
        this->push(Value((int) UNINITIALIZED));
    }
    for (int i = 0; i < f.fOwner.fGlobalCount; ++i) {
        fGlobals.push_back(Value((int) UNINITIALIZED));
    }
    for (int i = f.fOwner.fInputSlots.size() - 1; i >= 0; --i) {
        fGlobals[f.fOwner.fInputSlots[i]] = inputs[i];
    }
    run();
    int offset = 0;
    for (const auto& p : f.fDeclaration.fParameters) {
        if (p->fModifiers.fFlags & Modifiers::kOut_Flag) {
            for (int i = p->fType.columns() * p->fType.rows() - 1; i >= 0; --i) {
                args[offset] = fStack[offset];
                ++offset;
            }
        } else {
            offset += p->fType.columns() * p->fType.rows();
        }
    }
    return fStack.data();
}

struct CallbackCtx : public SkRasterPipeline_CallbackCtx {
    Interpreter* fInterpreter;
    const FunctionDefinition* fFunction;
};

#define READ8() code[ip++]

#define READ16()                                                  \
    (SkASSERT(ip % 2 == 0),                                       \
     ip += 2,                                                     \
     *(uint16_t*) &code[ip - 2])

#define READ32()                                                  \
    (SkASSERT(ip % 4 == 0),                                       \
     ip += 4,                                                     \
     *(uint32_t*) &code[ip - 4])

void Interpreter::push(Value v) {
    fStack.push_back(v);
}

Interpreter::Value Interpreter::pop() {
    Value v = fStack.back();
    fStack.pop_back();
    return v;
}

static String value_string(uint32_t v) {
    union { uint32_t u; float f; } pun = { v };
    return to_string(v) + "(" + to_string(pun.f) + ")";
}

void Interpreter::disassemble(const ByteCodeFunction& f) {
    int ip = 0;
    const uint8_t* code = f.fCode.data();
    while (ip < (int) f.fCode.size()) {
        printf("%d: ", ip);
        switch ((ByteCodeInstruction) READ8()) {
            case ByteCodeInstruction::kAddF: printf("addf"); break;
            case ByteCodeInstruction::kAddI: printf("addi"); break;
            case ByteCodeInstruction::kAndB: printf("andb"); break;
            case ByteCodeInstruction::kAndI: printf("andi"); break;
            case ByteCodeInstruction::kBranch: printf("branch %d", READ16()); break;
            case ByteCodeInstruction::kCompareIEQ: printf("comparei eq"); break;
            case ByteCodeInstruction::kCompareINEQ: printf("comparei neq"); break;
            case ByteCodeInstruction::kCompareFEQ: printf("comparef eq"); break;
            case ByteCodeInstruction::kCompareFGT: printf("comparef gt"); break;
            case ByteCodeInstruction::kCompareFGTEQ: printf("comparef gteq"); break;
            case ByteCodeInstruction::kCompareFLT: printf("comparef lt"); break;
            case ByteCodeInstruction::kCompareFLTEQ: printf("comparef lteq"); break;
            case ByteCodeInstruction::kCompareFNEQ: printf("comparef neq"); break;
            case ByteCodeInstruction::kCompareSGT: printf("compares sgt"); break;
            case ByteCodeInstruction::kCompareSGTEQ: printf("compares sgteq"); break;
            case ByteCodeInstruction::kCompareSLT: printf("compares lt"); break;
            case ByteCodeInstruction::kCompareSLTEQ: printf("compares lteq"); break;
            case ByteCodeInstruction::kCompareUGT: printf("compareu gt"); break;
            case ByteCodeInstruction::kCompareUGTEQ: printf("compareu gteq"); break;
            case ByteCodeInstruction::kCompareULT: printf("compareu lt"); break;
            case ByteCodeInstruction::kCompareULTEQ: printf("compareu lteq"); break;
            case ByteCodeInstruction::kConditionalBranch:
                printf("conditionalbranch %d", READ16());
                break;
            case ByteCodeInstruction::kDebugPrint: printf("debugprint"); break;
            case ByteCodeInstruction::kDivideF: printf("dividef"); break;
            case ByteCodeInstruction::kDivideS: printf("divides"); break;
            case ByteCodeInstruction::kDivideU: printf("divideu"); break;
            case ByteCodeInstruction::kDup: printf("dup"); break;
            case ByteCodeInstruction::kDupDown: printf("dupdown %d", READ8()); break;
            case ByteCodeInstruction::kFloatToInt: printf("floattoint"); break;
            case ByteCodeInstruction::kLoad: printf("load"); break;
            case ByteCodeInstruction::kLoadGlobal: printf("loadglobal %d", READ8()); break;
            case ByteCodeInstruction::kLoadSwizzle: {
                int count = READ8();
                printf("loadswizzle %d", count);
                for (int i = 0; i < count; ++i) {
                    printf(", %d", READ8());
                }
                break;
            }
            case ByteCodeInstruction::kMultiplyF: printf("multiplyf"); break;
            case ByteCodeInstruction::kMultiplyS: printf("multiplys"); break;
            case ByteCodeInstruction::kMultiplyU: printf("multiplyu"); break;
            case ByteCodeInstruction::kNegateF: printf("negatef"); break;
            case ByteCodeInstruction::kNegateS: printf("negates"); break;
            case ByteCodeInstruction::kNot: printf("not"); break;
            case ByteCodeInstruction::kOrB: printf("orb"); break;
            case ByteCodeInstruction::kOrI: printf("ori"); break;
            case ByteCodeInstruction::kParameter: printf("parameter"); break;
            case ByteCodeInstruction::kPop: printf("pop %d", READ8()); break;
            case ByteCodeInstruction::kPushImmediate:
                printf("pushimmediate %s", value_string(READ32()).c_str());
                break;
            case ByteCodeInstruction::kRemainderS: printf("remainders"); break;
            case ByteCodeInstruction::kRemainderU: printf("remainderu"); break;
            case ByteCodeInstruction::kReturn: printf("return %d", READ8()); break;
            case ByteCodeInstruction::kSignedToFloat: printf("signedtofloat"); break;
            case ByteCodeInstruction::kStore: printf("store"); break;
            case ByteCodeInstruction::kStoreGlobal: printf("storeglobal"); break;
            case ByteCodeInstruction::kStoreSwizzle: {
                int count = READ8();
                printf("storeswizzle %d", count);
                for (int i = 0; i < count; ++i) {
                    printf(", %d", READ8());
                }
                break;
            }
            case ByteCodeInstruction::kSubtractF: printf("subtractf"); break;
            case ByteCodeInstruction::kSubtractI: printf("subtracti"); break;
            case ByteCodeInstruction::kSwizzle: {
                printf("swizzle %d, ", READ8());
                int count = READ8();
                printf("%d", count);
                for (int i = 0; i < count; ++i) {
                    printf(", %d", READ8());
                }
                break;
            }
            case ByteCodeInstruction::kUnsignedToFloat: printf("unsignedtofloat"); break;
            case ByteCodeInstruction::kVector: printf("vector%d", READ8()); break;
            default: printf("%d\n", code[ip - 1]);
                     SkASSERT(false);
        }
        printf("\n");
    }
}

void Interpreter::dumpStack() {
    printf("STACK:");
    for (size_t i = 0; i < fStack.size(); ++i) {
        printf(" %d(%f)", fStack[i].fSigned, fStack[i].fFloat);
    }
    printf("\n");
}

#define BINARY_OP(inst, type, field, op) \
    case ByteCodeInstruction::inst: {    \
        type b = this->pop().field;      \
        Value* a = &fStack.back();       \
        *a = Value(a->field op b);       \
        break;                           \
    }

static constexpr int VECTOR_MAX = 16;

#define VECTOR_BINARY_OP(inst, type, field, op)               \
    case ByteCodeInstruction::inst: {                         \
        Value result[VECTOR_MAX];                             \
        for (int i = count - 1; i >= 0; --i) {                \
            result[i] = this->pop();                          \
        }                                                     \
        for (int i = count - 1; i >= 0; --i) {                \
            result[i] = this->pop().field op result[i].field; \
        }                                                     \
        for (int i = 0; i < count; ++i) {                     \
            this->push(result[i]);                            \
        }                                                     \
        break;                                                \
    }

void Interpreter::run() {
    int ip = 0;
    const uint8_t* code = fCurrentFunction->fCode.data();
    for (;;) {
#ifdef TRACE
        printf("at %d\n", ip);
#endif
        ByteCodeInstruction inst = (ByteCodeInstruction) READ8();
        switch (inst) {
            BINARY_OP(kAddI, int32_t, fSigned, +)
            BINARY_OP(kAddF, float, fFloat, +)
            case ByteCodeInstruction::kBranch:
                ip = READ16();
                break;
            BINARY_OP(kCompareIEQ, int32_t, fSigned, ==)
            BINARY_OP(kCompareFEQ, float, fFloat, ==)
            BINARY_OP(kCompareINEQ, int32_t, fSigned, !=)
            BINARY_OP(kCompareFNEQ, float, fFloat, !=)
            BINARY_OP(kCompareSGT, int32_t, fSigned, >)
            BINARY_OP(kCompareUGT, uint32_t, fUnsigned, >)
            BINARY_OP(kCompareFGT, float, fFloat, >)
            BINARY_OP(kCompareSGTEQ, int32_t, fSigned, >=)
            BINARY_OP(kCompareUGTEQ, uint32_t, fUnsigned, >=)
            BINARY_OP(kCompareFGTEQ, float, fFloat, >=)
            BINARY_OP(kCompareSLT, int32_t, fSigned, <)
            BINARY_OP(kCompareULT, uint32_t, fUnsigned, <)
            BINARY_OP(kCompareFLT, float, fFloat, <)
            BINARY_OP(kCompareSLTEQ, int32_t, fSigned, <=)
            BINARY_OP(kCompareULTEQ, uint32_t, fUnsigned, <=)
            BINARY_OP(kCompareFLTEQ, float, fFloat, <=)
            case ByteCodeInstruction::kConditionalBranch: {
                int target = READ16();
                if (this->pop().fBool) {
                    ip = target;
                }
                break;
            }
            case ByteCodeInstruction::kDebugPrint: {
                Value v = this->pop();
                printf("Debug: %d(int), %d(uint), %f(float)\n", v.fSigned, v.fUnsigned, v.fFloat);
                break;
            }
            BINARY_OP(kDivideS, int32_t, fSigned, /)
            BINARY_OP(kDivideU, uint32_t, fUnsigned, /)
            BINARY_OP(kDivideF, float, fFloat, /)
            case ByteCodeInstruction::kDup:
                this->push(fStack.back());
                break;
            case ByteCodeInstruction::kDupDown: {
                int count = READ8();
                for (int i = 0; i < count; ++i) {
                    fStack.insert(fStack.end() - i - count - 1, fStack[fStack.size() - i - 1]);
                }
                break;
            }
            case ByteCodeInstruction::kFloatToInt: {
                Value& top = fStack.back();
                top.fSigned = (int) top.fFloat;
                break;
            }
            case ByteCodeInstruction::kSignedToFloat: {
                Value& top = fStack.back();
                top.fFloat = (float) top.fSigned;
                break;
            }
            case ByteCodeInstruction::kUnsignedToFloat: {
                Value& top = fStack.back();
                top.fFloat = (float) top.fUnsigned;
                break;
            }
            case ByteCodeInstruction::kLoad: {
                int target = this->pop().fSigned;
                SkASSERT(target < (int) fStack.size());
                this->push(fStack[target]);
                break;
            }
            case ByteCodeInstruction::kLoadGlobal: {
                int target = READ8();
                SkASSERT(target < (int) fGlobals.size());
                this->push(fGlobals[target]);
                break;
            }
            case ByteCodeInstruction::kLoadSwizzle: {
                Value target = this->pop();
                int count = READ8();
                for (int i = 0; i < count; ++i) {
                    SkASSERT(target.fSigned + fCurrentFunction->fCode[ip + i] < (int) fStack.size());
                    this->push(fStack[target.fSigned + fCurrentFunction->fCode[ip + i]]);
                }
                ip += count;
                break;
            }
            BINARY_OP(kMultiplyS, int32_t, fSigned, *)
            BINARY_OP(kMultiplyU, uint32_t, fUnsigned, *)
            BINARY_OP(kMultiplyF, float, fFloat, *)
            case ByteCodeInstruction::kNot: {
                Value& top = fStack.back();
                top.fBool = !top.fBool;
                break;
            }
            case ByteCodeInstruction::kNegateF: {
                Value& top = fStack.back();
                top.fFloat = -top.fFloat;
                break;
            }
            case ByteCodeInstruction::kNegateS: {
                Value& top = fStack.back();
                top.fSigned = -top.fSigned;
                break;
            }
            case ByteCodeInstruction::kNop:
                break;
            case ByteCodeInstruction::kPop:
                for (int i = READ8(); i > 0; --i) {
                    this->pop();
                }
                break;
            case ByteCodeInstruction::kPushImmediate:
                this->push(Value((int) READ32()));
                break;
            BINARY_OP(kRemainderS, int32_t, fSigned, %)
            BINARY_OP(kRemainderU, uint32_t, fUnsigned, %)
            case ByteCodeInstruction::kReturn: {
                int count = READ8();
                for (int i = 0; i < count; ++i) {
                    fStack[i] = fStack[fStack.size() - count + i];
                }
                return;
            }
            case ByteCodeInstruction::kStore: {
                Value value = this->pop();
                int target = this->pop().fSigned;
                SkASSERT(target < (int) fStack.size());
                fStack[target] = value;
                break;
            }
            case ByteCodeInstruction::kStoreGlobal: {
                Value value = this->pop();
                int target = this->pop().fSigned;
                SkASSERT(target < (int) fGlobals.size());
                fGlobals[target] = value;
                break;
            }
            case ByteCodeInstruction::kStoreSwizzle: {
                int count = READ8();
                int target = fStack[fStack.size() - count - 1].fSigned;
                for (int i = count - 1; i >= 0; --i) {
                    SkASSERT(target + fCurrentFunction->fCode[ip + i] < (int) fStack.size());
                    fStack[target + fCurrentFunction->fCode[ip + i]] = this->pop();
                }
                this->pop();
                ip += count;
                break;
            }
            BINARY_OP(kSubtractI, int32_t, fSigned, -)
            BINARY_OP(kSubtractF, float, fFloat, -)
            case ByteCodeInstruction::kSwizzle: {
                Value vec[4];
                for (int i = READ8() - 1; i >= 0; --i) {
                    vec[i] = this->pop();
                }
                for (int i = READ8() - 1; i >= 0; --i) {
                    this->push(vec[READ8()]);
                }
                break;
            }
            case ByteCodeInstruction::kVector: {
                uint8_t count = READ8();
                ByteCodeInstruction inst = (ByteCodeInstruction) READ8();
                switch (inst) {
                    VECTOR_BINARY_OP(kAddI, int32_t, fSigned, +)
                    VECTOR_BINARY_OP(kAddF, float, fFloat, +)
                    case ByteCodeInstruction::kBranch:
                        ip = READ16();
                        break;
                    VECTOR_BINARY_OP(kCompareIEQ, int32_t, fSigned, ==)
                    VECTOR_BINARY_OP(kCompareFEQ, float, fFloat, ==)
                    VECTOR_BINARY_OP(kCompareINEQ, int32_t, fSigned, !=)
                    VECTOR_BINARY_OP(kCompareFNEQ, float, fFloat, !=)
                    VECTOR_BINARY_OP(kCompareSGT, int32_t, fSigned, >)
                    VECTOR_BINARY_OP(kCompareUGT, uint32_t, fUnsigned, >)
                    VECTOR_BINARY_OP(kCompareFGT, float, fFloat, >)
                    VECTOR_BINARY_OP(kCompareSGTEQ, int32_t, fSigned, >=)
                    VECTOR_BINARY_OP(kCompareUGTEQ, uint32_t, fUnsigned, >=)
                    VECTOR_BINARY_OP(kCompareFGTEQ, float, fFloat, >=)
                    VECTOR_BINARY_OP(kCompareSLT, int32_t, fSigned, <)
                    VECTOR_BINARY_OP(kCompareULT, uint32_t, fUnsigned, <)
                    VECTOR_BINARY_OP(kCompareFLT, float, fFloat, <)
                    VECTOR_BINARY_OP(kCompareSLTEQ, int32_t, fSigned, <=)
                    VECTOR_BINARY_OP(kCompareULTEQ, uint32_t, fUnsigned, <=)
                    VECTOR_BINARY_OP(kCompareFLTEQ, float, fFloat, <=)
                    case ByteCodeInstruction::kConditionalBranch: {
                        int target = READ16();
                        if (this->pop().fBool) {
                            ip = target;
                        }
                        break;
                    }
                    VECTOR_BINARY_OP(kDivideS, int32_t, fSigned, /)
                    VECTOR_BINARY_OP(kDivideU, uint32_t, fUnsigned, /)
                    VECTOR_BINARY_OP(kDivideF, float, fFloat, /)
                    case ByteCodeInstruction::kFloatToInt: {
                        for (int i = 0; i < count; ++i) {
                            Value& v = fStack[fStack.size() - i - 1];
                            v.fSigned = (int) v.fFloat;
                        }
                        break;
                    }
                    case ByteCodeInstruction::kSignedToFloat: {
                        for (int i = 0; i < count; ++i) {
                            Value& v = fStack[fStack.size() - i - 1];
                            v.fFloat = (float) v.fSigned;
                        }
                        break;
                    }
                    case ByteCodeInstruction::kUnsignedToFloat: {
                        for (int i = 0; i < count; ++i) {
                            Value& v = fStack[fStack.size() - i - 1];
                            v.fFloat = (float) v.fUnsigned;
                        }
                        break;
                    }
                    case ByteCodeInstruction::kLoad: {
                        int target = this->pop().fSigned;
                        for (int i = 0; i < count; ++i) {
                            SkASSERT(target < (int) fStack.size());
                            this->push(fStack[target++]);
                        }
                        break;
                    }
                    case ByteCodeInstruction::kLoadGlobal: {
                        int target = READ8();
                        SkASSERT(target < (int) fGlobals.size());
                        this->push(fGlobals[target]);
                        break;
                    }
                    VECTOR_BINARY_OP(kMultiplyS, int32_t, fSigned, *)
                    VECTOR_BINARY_OP(kMultiplyU, uint32_t, fUnsigned, *)
                    VECTOR_BINARY_OP(kMultiplyF, float, fFloat, *)
                    VECTOR_BINARY_OP(kRemainderS, int32_t, fSigned, %)
                    VECTOR_BINARY_OP(kRemainderU, uint32_t, fUnsigned, %)
                    case ByteCodeInstruction::kStore: {
                        int target = fStack[fStack.size() - count - 1].fSigned + count;
                        for (int i = count - 1; i >= 0; --i) {
                            SkASSERT(target < (int) fStack.size());
                            fStack[--target] = this->pop();
                        }
                        break;
                    }
                    VECTOR_BINARY_OP(kSubtractI, int32_t, fSigned, -)
                    VECTOR_BINARY_OP(kSubtractF, float, fFloat, -)
                    default:
                        printf("unsupported instruction %d\n", (int) inst);
                        SkASSERT(false);
                }
                break;
            }
            default:
                printf("unsupported instruction %d\n", (int) inst);
                SkASSERT(false);
        }
#ifdef TRACE
        this->dumpStack();
#endif
    }
}

} // namespace

#endif

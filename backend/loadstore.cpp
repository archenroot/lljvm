/*
* Copyright (c) 2009 David Roberts <d@vidr.cc>
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

#include "backend.h"

void JVMWriter::printValueLoad(const Value *v) {
    if(const Function *f = dyn_cast<Function>(v)) {
        std::string name = getValueName(f)
                         + getCallSignature(f->getFunctionType());
        // TODO: push pointer to function onto stack
        llvm_unreachable("Function pointers not yet supported");
    } else if(isa<GlobalVariable>(v)) {
        const Type *ty = cast<PointerType>(v->getType())->getElementType();
        if(externRefs.count(v))
            printSimpleInstruction("getstatic",
                getValueName(v) + ' ' + getTypeDescriptor(ty));
        else
            printSimpleInstruction("getstatic",
                classname + "/" + getValueName(v)
                + ' ' + getTypeDescriptor(ty));
    } else if(isa<ConstantPointerNull>(v)) {
        printPtrLoad(0);
    } else if(const ConstantExpr *ce = dyn_cast<ConstantExpr>(v)) {
        printConstantExpr(ce);
    } else if(const Constant *c = dyn_cast<Constant>(v)) {
        printConstLoad(c);
    } else {
        if(getLocalVarNumber(v) <= 3)
            printSimpleInstruction(
                getTypePrefix(v->getType(), true) + "load_"
                + utostr(getLocalVarNumber(v))
                + " ; " + getValueName(v));
        else
            printSimpleInstruction(
                getTypePrefix(v->getType(), true) + "load",
                utostr(getLocalVarNumber(v))
                + " ; " + getValueName(v));
    }
}

void JVMWriter::printValueStore(const Value *v) {
    if(isa<Function>(v) || isa<GlobalVariable>(v) || isa<Constant>(v)) {
        errs() << "Value  = " << *v << '\n';
        llvm_unreachable("Invalid value");
    }
    if(getLocalVarNumber(v) <= 3)
        printSimpleInstruction(
            getTypePrefix(v->getType(), true) + "store_"
            + utostr(getLocalVarNumber(v))
            + " ; " + getValueName(v));
    else
        printSimpleInstruction(
            getTypePrefix(v->getType(), true) + "store",
            utostr(getLocalVarNumber(v))
            + " ; " + getValueName(v));
}

void JVMWriter::printIndirectLoad(const Value *v) {
    printValueLoad(v);
    const Type *ty = v->getType();
    if(const PointerType *p = dyn_cast<PointerType>(ty))
        ty = p->getElementType();
    printIndirectLoad(ty);
}

void JVMWriter::printIndirectLoad(const Type *ty) {
    printSimpleInstruction("invokestatic", "lljvm/runtime/Memory/load_"
        + getTypePostfix(ty) + "(I)" + getTypeDescriptor(ty));
}

void JVMWriter::printIndirectStore(const Value *ptr, const Value *val) {
    printValueLoad(ptr);
    printValueLoad(val);
    printIndirectStore(val->getType());
}

void JVMWriter::printIndirectStore(const Type *ty) {
    printSimpleInstruction("invokestatic",
        "lljvm/runtime/Memory/store(I" + getTypeDescriptor(ty) + ")V");
}

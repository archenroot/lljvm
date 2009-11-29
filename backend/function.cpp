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

std::string JVMWriter::getCallSignature(const FunctionType *ty) {
    std::string sig;
    sig += '(';
    for(unsigned int i = 0, e = ty->getNumParams(); i < e; i++)
        sig += getTypeDescriptor(ty->getParamType(i));
    if(ty->isVarArg()) sig += "I";
    sig += ')';
    sig += getTypeDescriptor(ty->getReturnType());
    return sig;
}

void JVMWriter::printVarargPack(const Function *f, const Instruction *inst) {
    const FunctionType *ty = f->getFunctionType();
    unsigned int origin = isa<InvokeInst>(inst) ? 3 : 1;
    unsigned int numParams = ty->getNumParams();
    unsigned int numOperands = inst->getNumOperands() - origin;
    
    unsigned int valistSize = 0;
    for(unsigned int i = numParams; i < numOperands; i++)
        valistSize += targetData->getTypeAllocSize(
            inst->getOperand(i + origin)->getType());

    printSimpleInstruction("bipush", utostr(valistSize));
    printSimpleInstruction("invokestatic",
                           "lljvm/runtime/Memory/allocateStack(I)I");
    printSimpleInstruction("dup");

    for(unsigned int i = numParams; i < numOperands; i++) {
        const Value *v = inst->getOperand(i + origin);
        printValueLoad(v);
        printSimpleInstruction("invokestatic",
            "lljvm/runtime/Memory/pack(I"
            + getTypeDescriptor(v->getType()) + ")I");
    }
    printSimpleInstruction("pop");
}

void JVMWriter::printFunctionCall(const Value *functionVal,
                                  const Instruction *inst) {
    if(const Function *f = dyn_cast<Function>(functionVal)) { // direct call
        const FunctionType *ty = f->getFunctionType();
        unsigned int origin = isa<InvokeInst>(inst) ? 3 : 1;
        
        //for(unsigned int i = origin, e = inst->getNumOperands(); i < e; i++)
        //    printValueLoad(inst->getOperand(i));
        
        for(unsigned int i = 0, e = ty->getNumParams(); i < e; i++)
            printValueLoad(inst->getOperand(i + origin));
        if(ty->isVarArg() && inst)
            printVarargPack(f, inst);
        
        if(externRefs.count(f))
            printSimpleInstruction("invokestatic",
                getValueName(f) + getCallSignature(ty));
        else
            printSimpleInstruction("invokestatic",
                classname + "/" + getValueName(f) + getCallSignature(ty));
    } else { // indirect call
        printValueLoad(functionVal);
        const FunctionType *ty = cast<FunctionType>(
            cast<PointerType>(functionVal->getType())->getElementType());
        std::string sig = getCallSignature(ty);
        // TODO: indirectly invoke function
        errs() << "TypeSig = " << sig << '\n';
        llvm_unreachable("Indirect function calls not yet supported");
    }
}

void JVMWriter::printIntrinsicCall(const IntrinsicInst *inst) {
    const Type *valistTy = PointerType::getUnqual(
        IntegerType::get(inst->getContext(), 8));
    switch(inst->getIntrinsicID()) {
    case Intrinsic::vastart:
        printValueLoad(inst->getOperand(1));
        printSimpleInstruction("iload", utostr(vaArgNum) + " ; varargptr");
        printIndirectStore(valistTy);
        break;
    case Intrinsic::vacopy:
        printValueLoad(inst->getOperand(1));
        printValueLoad(inst->getOperand(2));
        printIndirectLoad(valistTy);
        printIndirectStore(valistTy);
        break;    
    case Intrinsic::vaend:
        break;    
    default:
    errs() << "Intrinsic ID = " << inst->getIntrinsicID() << '\n';
    llvm_unreachable("Invalid intrinsic function");
  }
}

void JVMWriter::printCallInstruction(const Instruction *inst) {
    if(isa<IntrinsicInst>(inst))
        printIntrinsicCall(cast<IntrinsicInst>(inst));
    else
        printFunctionCall(inst->getOperand(0), inst);
}

void JVMWriter::printInvokeInstruction(const InvokeInst *inst) {
    std::string labelname = getLabelName(inst) + "$invoke";
    printLabel(labelname + "_begin");
    printFunctionCall(inst->getOperand(0), inst);
    printValueStore(inst); // save return value
    printLabel(labelname + "_end");
    printBranchToBlock(inst->getParent(), NULL, inst->getNormalDest());
    printLabel(labelname + "_catch");
    printSimpleInstruction("pop");
    printBranchToBlock(inst->getParent(), NULL, inst->getUnwindDest());
    printSimpleInstruction(".catch lljvm/runtime/System$Unwind",
          "from "  + labelname + "_begin "
        + "to "    + labelname + "_end "
        + "using " + labelname + "_catch");
}

void JVMWriter::printLocalVariable(const Function &f,
                                   const Instruction *inst) {
    const AllocaInst *ai = dyn_cast<AllocaInst>(inst);
    if(ai && !isa<GlobalVariable>(ai)) {
        // local variable allocation
        const Type *ty = PointerType::getUnqual(ai->getAllocatedType());
        printSimpleInstruction(".var " + utostr(getLocalVarNumber(ai)),
            "is " + getValueName(ai) + ' ' + getTypeDescriptor(ty)
            + " from begin_method to end_method");
        // initialise local variable
        printSimpleInstruction(getTypePrefix(ty, true) + "const_0");
        printSimpleInstruction(getTypePrefix(ty, true) + "store",
            utostr(getLocalVarNumber(ai)));
    } else if(inst->getType() != Type::getVoidTy(f.getContext())) {
        // operation result
        printSimpleInstruction(".var " + utostr(getLocalVarNumber(inst)),
            "is " + getValueName(inst) + ' '
            + getTypeDescriptor(inst->getType())
            + " from begin_method to end_method");
    }
}

void JVMWriter::printFunctionBody(const Function &f) {
    for(Function::const_iterator i = f.begin(), e = f.end(); i != e; i++) {
        if(Loop *l = getAnalysis<LoopInfo>().getLoopFor(i)) {
            if(l->getHeader() == i && l->getParentLoop() == 0)
                printLoop(l);
        } else
            printBasicBlock(i);
    }
}

unsigned int JVMWriter::getLocalVarNumber(const Value *v) {
    if(!localVars.count(v))
        localVars[v] = usedRegisters++;
    if(getBitWidth(v->getType()) == 64)
        usedRegisters++; // 64 bit types occupy 2 registers
    return localVars[v];
}

void JVMWriter::printFunction(const Function &f) {
    localVars.clear();
    usedRegisters = 0;
    
    out << '\n';
    out << ".method " << (f.hasLocalLinkage() ? "private " : "public ")
        << "static " << getValueName(&f) << '(';
    for(Function::const_arg_iterator i = f.arg_begin(), e = f.arg_end();
        i != e; i++)
        out << getTypeDescriptor(i->getType());
    if(f.isVarArg())
        out << "I";
    out << ')' << getTypeDescriptor(f.getReturnType()) << '\n';
    
    for(Function::const_arg_iterator i = f.arg_begin(), e = f.arg_end();
        i != e; i++)
            printSimpleInstruction(".var " + utostr(getLocalVarNumber(i)),
                "is " + getValueName(i) + ' ' + getTypeDescriptor(i->getType())
                + " from begin_method to end_method");
    if(f.isVarArg()) {
        vaArgNum = usedRegisters++;
        printSimpleInstruction(".var " + utostr(vaArgNum),
            "is varargptr I from begin_method to end_method");
    }
    
    // TODO: better stack depth analysis
    unsigned int stackDepth = 8;
    for(const_inst_iterator i = inst_begin(&f), e = inst_end(&f);
        i != e; i++) {
        if(stackDepth < i->getNumOperands())
            stackDepth = i->getNumOperands();
        printLocalVariable(f, &*i);
    }
    printSimpleInstruction(".limit stack", utostr(stackDepth * 2));
    printSimpleInstruction(".limit locals", utostr(usedRegisters));
    
    printLabel("begin_method");
    printSimpleInstruction("invokestatic",
                           "lljvm/runtime/Memory/createStackFrame()V");
    printFunctionBody(f);
    printLabel("end_method");
    out << ".end method\n";
}

#include "engine.hpp"

using namespace yeet;

#include "../edn/edn.hpp"



Engine::Engine() {
    initializeLLVM();
}

Engine::~Engine() {
    // Cleanup if necessary
}

void Engine::initializeLLVM()
{
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    jit = std::move(*llvm::orc::LLJITBuilder().create());
    context = std::make_unique<llvm::LLVMContext>();
}


void Engine::run(std::string& s)
{
    symbolTable.clear();
    auto node = edn::read(s);
    auto module = std::make_unique<llvm::Module>("calc_module", *context);
    llvm::IRBuilder<> builder(*context);
    // Create function prototype: double calc()
    auto funcType = llvm::FunctionType::get(builder.getDoubleTy(), false);
    auto func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "calc", module.get());
    auto entry = llvm::BasicBlock::Create(*context, "entry", func);
    builder.SetInsertPoint(entry);
    llvm::Value* result = nullptr;
    try {
        result = this->codegenExpr(node, *context, builder);
    } catch (const char* msg) {
        std::cerr << "Codegen error: " << msg << std::endl;
        return;
    } catch (const std::string& msg) {
        std::cerr << "Codegen error: " << msg << std::endl;
        return;
    }
    // Only emit return if result is not nullptr (i.e., not a defn)
    if (result) {
        // If result is int, cast to double before returning
        if (result->getType()->isIntegerTy()) {
            result = builder.CreateSIToFP(result, builder.getDoubleTy(), "intToDouble");
        }
        builder.CreateRet(result);
    } else {
        // If top-level was a defn, try to call 'main' if it exists
        auto& moduleRef = *module;
        llvm::Function* mainFunc = moduleRef.getFunction("main");
        if (mainFunc) {
            llvm::IRBuilder<> callBuilder(entry);
            llvm::Value* callResult = callBuilder.CreateCall(mainFunc, {}, "callmain");
            callBuilder.CreateRet(callResult);
        } else {
            // No value to return, just return 0.0
            builder.CreateRet(llvm::ConstantFP::get(builder.getDoubleTy(), 0.0));
        }
    }

    // Print the generated LLVM IR
    std::cout << "\n===== Generated LLVM IR =====\n";
    module->print(llvm::outs(), nullptr);
    std::cout << "\n============================\n" << std::endl;

    // Add module to JIT
    if (auto err = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
        std::cerr << "Failed to add module to JIT: " << llvm::toString(std::move(err)) << std::endl;
        return;
    }
    // Look up and run the function
    auto sym = jit->lookup("calc");
    if (!sym) {
        std::cerr << "Failed to find function: " << llvm::toString(sym.takeError()) << std::endl;
        return;
    }
    try {
        auto calcFn = sym->toPtr<double(*)()>();
    
        double value = calcFn();
        std::cout << "JIT result: " << value << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error executing JIT function: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Unknown error executing JIT function." << std::endl;
    }
    
}



// Helper for EdnInt
llvm::Value* Engine::codegenInt(const edn::EdnNode& node, llvm::IRBuilder<>& builder) {
    return llvm::ConstantInt::get(builder.getInt32Ty(), std::stoi(node.value));
}

// Helper for EdnFloat
llvm::Value* Engine::codegenFloat(const edn::EdnNode& node, llvm::IRBuilder<>& builder) {
    return llvm::ConstantFP::get(builder.getDoubleTy(), std::stod(node.value));
}

// Helper for EdnSymbol
llvm::Value* Engine::codegenSymbol(const edn::EdnNode& node, llvm::IRBuilder<>& builder) {
    if (node.value == "else") {
        // 'else' is a special keyword in cond, not a variable
        return llvm::ConstantInt::get(builder.getInt32Ty(), 1);
    }
    auto it = symbolTable.find(node.value);
    if (it == symbolTable.end()) throw std::string("Unknown variable: ") + node.value;
    return builder.CreateLoad(builder.getInt32Ty(), it->second, node.value);
}

llvm::Value* Engine::codegenAssign(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    // Assignment: (= var value)
    if (node.values.size() != 3) throw "Expected variable and value";
    const EdnNode& varNode = *(++node.values.begin());
    const EdnNode& valNode = *(++++node.values.begin());
    if (varNode.type != EdnSymbol) throw "Expected variable name";
    llvm::Value* value = this->codegenExpr(valNode, context, builder);
    auto it = symbolTable.find(varNode.value);
    llvm::Value* alloca = nullptr;
    if (it == symbolTable.end()) {
        alloca = builder.CreateAlloca(builder.getInt32Ty(), nullptr, varNode.value);
        symbolTable[varNode.value] = alloca;
    } else {
        alloca = it->second;
    }
    builder.CreateStore(value, alloca);
    return value;
}

// Helper for EdnList
llvm::Value* Engine::codegenList(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    // If this is a sequence of expressions (not an operation), evaluate each and return the last
    bool allAreLists = true;
    for (const auto& v : node.values) {
        if (v.type != EdnList && v.type != EdnInt && v.type != EdnSymbol && v.type != EdnFloat) {
            allAreLists = false;
            break;
        }
    }
    if (allAreLists && node.values.size() > 1 && node.values.front().type == EdnList) {
        llvm::Value* last = nullptr;
        for (const auto& expr : node.values) {
            last = this->codegenExpr(expr, context, builder);
        }
        return last;
    }
    // Otherwise, treat as operation
    const EdnNode& opNode = node.values.front();
    if (opNode.type != EdnSymbol) throw "Expected operator symbol";
    std::string op = opNode.value;
    if (op == "defn") {
        return this->codegenDefn(node, context, builder);
    }
    if (op == "cond") {
        return this->codegenCond(node, context, builder);
    }
    if (op == "=") {
        return this->codegenAssign(node, context, builder);
    }
    if (op == "while") {
        return this->codegenWhile(node, context, builder);
    }
    if(op == "+" || op == "-" || op == "*" || op == "/" || op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        return this->codegenBinop(node, context, builder);
    }
    // Function call: (name arg1 arg2 ...)
    if (opNode.type == edn::EdnSymbol && functionTable.count(op) > 0) {
        return this->codegenCall(node, context, builder);
    }
    throw std::string("Unknown operator: ") + op;
}

// (defn name (args...) body...)
llvm::Value* Engine::codegenDefn(const edn::EdnNode& node, llvm::LLVMContext&, llvm::IRBuilder<>&) {
    using namespace edn;
    if (node.values.size() < 4) throw "defn requires a name, arg list, and body";
    const EdnNode& nameNode = *(++node.values.begin());
    const EdnNode& argsNode = *(++++node.values.begin());
    if (nameNode.type != EdnSymbol) throw "defn: function name must be a symbol";
    if (argsNode.type != EdnList) throw "defn: argument list must be a list";
    std::vector<std::string> args;
    for (const auto& arg : argsNode.values) {
        if (arg.type != EdnSymbol) throw "defn: all arguments must be symbols";
        args.push_back(arg.value);
    }
    // Store the function definition (args, body...)
    EdnNode bodyNode = node;
    bodyNode.values.erase(bodyNode.values.begin(), std::next(bodyNode.values.begin(), 3));
    functionTable[nameNode.value] = {args, bodyNode};
    return nullptr; // defn does not produce a value
}

// (name arg1 arg2 ...)
llvm::Value* Engine::codegenCall(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    const EdnNode& opNode = node.values.front();
    auto it = functionTable.find(opNode.value);
    if (it == functionTable.end()) throw std::string("Unknown function: ") + opNode.value;
    const auto& [args, bodyNode] = it->second;
    if (node.values.size() - 1 != args.size()) throw "Function argument count mismatch";
    // Create function type: double(args...)
    std::vector<llvm::Type*> argTypes(args.size(), builder.getDoubleTy());
    auto funcType = llvm::FunctionType::get(builder.getDoubleTy(), argTypes, false);
    auto& module = *builder.GetInsertBlock()->getModule();
    llvm::Function* func = module.getFunction(opNode.value);
    if (!func) {
        func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, opNode.value, &module);
        // Create entry block for function
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context, "entry", func);
        llvm::IRBuilder<> funcBuilder(entry);
        // Set up argument symbol table
        auto argIt = func->arg_begin();
        for (size_t i = 0; i < args.size(); ++i, ++argIt) {
            llvm::Value* alloca = funcBuilder.CreateAlloca(builder.getDoubleTy(), nullptr, args[i]);
            funcBuilder.CreateStore(&*argIt, alloca);
            symbolTable[args[i]] = alloca;
        }
        // Evaluate body (could be multiple expressions)
        llvm::Value* result = nullptr;
        for (const auto& expr : bodyNode.values) {
            result = this->codegenExpr(expr, context, funcBuilder);
        }
        if (result->getType()->isIntegerTy()) {
            result = funcBuilder.CreateSIToFP(result, builder.getDoubleTy(), "intToDouble");
        }
        funcBuilder.CreateRet(result);
    }
    // Prepare call arguments
    std::vector<llvm::Value*> callArgs;
    for (auto it = std::next(node.values.begin()); it != node.values.end(); ++it) {
        llvm::Value* argVal = this->codegenExpr(*it, context, builder);
        if (argVal->getType()->isIntegerTy()) {
            argVal = builder.CreateSIToFP(argVal, builder.getDoubleTy(), "intToDouble");
        }
        callArgs.push_back(argVal);
    }
    return builder.CreateCall(func, callArgs, "calltmp");
}

llvm::Value* Engine::codegenWhile(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    // (while test body)
    if (node.values.size() != 3) throw "while requires a test and a body";
    const EdnNode& testNode = *(++node.values.begin());
    const EdnNode& bodyNode = *(++++node.values.begin());
    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* condBB = llvm::BasicBlock::Create(context, "while.cond", function);
    llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context, "while.body", function);
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(context, "while.after", function);
    builder.CreateBr(condBB);
    // Condition block
    builder.SetInsertPoint(condBB);
    llvm::Value* condVal = this->codegenExpr(testNode, context, builder);
    if (condVal->getType()->isFloatingPointTy()) {
        condVal = builder.CreateFCmpONE(condVal, llvm::ConstantFP::get(condVal->getType(), 0.0), "whilecond");
    } else {
        condVal = builder.CreateICmpNE(condVal, llvm::ConstantInt::get(condVal->getType(), 0), "whilecond");
    }
    builder.CreateCondBr(condVal, bodyBB, afterBB);
    // Body block
    builder.SetInsertPoint(bodyBB);
    this->codegenExpr(bodyNode, context, builder);
    builder.CreateBr(condBB);
    // After block
    builder.SetInsertPoint(afterBB);
    // Return 0.0 as the value of the while loop (could be changed to last body value if desired)
    return llvm::ConstantFP::get(builder.getDoubleTy(), 0.0);
}


// Main dispatcher
llvm::Value* Engine::codegenExpr(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    switch (node.type) {
        case EdnInt:
            return codegenInt(node, builder);
        case EdnFloat:
            return codegenFloat(node, builder);
        case EdnSymbol:
            return codegenSymbol(node, builder);
        case EdnList:
            return codegenList(node, context, builder);
        default:
            throw "Unsupported expression";
    }
}

// Helper for cond special form
llvm::Value* Engine::codegenCond(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    // (cond (test1 expr1) (test2 expr2) ... (else exprN))
    if (node.values.size() < 2) throw "cond requires at least one clause";
    llvm::Function* function = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* afterBB = llvm::BasicBlock::Create(context, "cond.after", function);
    llvm::PHINode* phi = nullptr;
    // Only create and fill reachable clause blocks
    std::vector<std::pair<const edn::EdnNode*, llvm::BasicBlock*>> clauses;
    llvm::BasicBlock* dispatchBB = builder.GetInsertBlock();
    size_t lastDispatched = 0;
    auto it = std::next(node.values.begin());
    for (; it != node.values.end(); ++it) {
        clauses.emplace_back(&(*it), llvm::BasicBlock::Create(context, "cond.clause", function));
        const edn::EdnNode* clause = &(*it);
        auto clauseIt = clause->values.begin();
        const edn::EdnNode* testNode = nullptr;
        if (clause->values.size() == 2) {
            testNode = &(*clauseIt);
        }
        builder.SetInsertPoint(dispatchBB);
        llvm::Value* testVal = nullptr;
        if (clause->values.size() == 2) {
            testVal = this->codegenExpr(*testNode, context, builder);
        }
        if (clause->values.size() == 1 || (testNode && testNode->type == edn::EdnSymbol && testNode->value == "else") || std::next(it) == node.values.end()) {
            builder.CreateBr(clauses.back().second);
            lastDispatched = clauses.size();
            ++it;
            break;
        } else {
            llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(context, "cond.clause", function);
            builder.CreateCondBr(testVal, clauses.back().second, nextBB);
            dispatchBB = nextBB;
            lastDispatched = clauses.size();
        }
    }
    // Now fill in each clause block (only up to lastDispatched)
    phi = llvm::PHINode::Create(builder.getDoubleTy(), lastDispatched, "condresult", afterBB);
    for (size_t i = 0; i < lastDispatched; ++i) {
        llvm::BasicBlock* clauseBB = clauses[i].second;
        builder.SetInsertPoint(clauseBB);
        const edn::EdnNode* clause = clauses[i].first;
        auto exprIt = clause->values.end();
        --exprIt;
        const edn::EdnNode& exprNode = *exprIt;
        llvm::Value* exprVal = this->codegenExpr(exprNode, context, builder);
        llvm::Value* castVal = exprVal;
        if (exprVal->getType()->isIntegerTy()) {
            castVal = builder.CreateSIToFP(exprVal, builder.getDoubleTy(), "intToDouble");
        } else if (exprVal->getType()->isFloatTy() && !exprVal->getType()->isDoubleTy()) {
            castVal = builder.CreateFPExt(exprVal, builder.getDoubleTy(), "floatToDouble");
        }
        builder.CreateBr(afterBB);
        phi->addIncoming(castVal, clauseBB);
    }
    builder.SetInsertPoint(afterBB);
    return phi;
}

llvm::Value* Engine::codegenBinop(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder)
{
    using namespace edn;
    const EdnNode& opNode = node.values.front();
    std::string op = opNode.value;
    
    if (node.values.size() != 3) throw "Expected two operands";
    auto lhsIt = ++node.values.begin();
    auto rhsIt = ++++node.values.begin();
    llvm::Value* lhs = this->codegenExpr(*lhsIt, context, builder);
    llvm::Value* rhs = this->codegenExpr(*rhsIt, context, builder);

    bool lhsIsFloat = lhsIt->type == EdnFloat;
    bool rhsIsFloat = rhsIt->type == EdnFloat;
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        if (lhsIsFloat || rhsIsFloat) {
            // Promote ints to double if needed
            if (!lhs->getType()->isDoubleTy()) lhs = builder.CreateSIToFP(lhs, builder.getDoubleTy(), "intToDoubleL");
            if (!rhs->getType()->isDoubleTy()) rhs = builder.CreateSIToFP(rhs, builder.getDoubleTy(), "intToDoubleR");
            if (op == "==") return builder.CreateUIToFP(builder.CreateFCmpUEQ(lhs, rhs, "cmptmp"), builder.getInt32Ty());
            if (op == "!=") return builder.CreateUIToFP(builder.CreateFCmpUNE(lhs, rhs, "cmptmp"), builder.getInt32Ty());
            if (op == "<")  return builder.CreateUIToFP(builder.CreateFCmpULT(lhs, rhs, "cmptmp"), builder.getInt32Ty());
            if (op == "<=") return builder.CreateUIToFP(builder.CreateFCmpULE(lhs, rhs, "cmptmp"), builder.getInt32Ty());
            if (op == ">")  return builder.CreateUIToFP(builder.CreateFCmpUGT(lhs, rhs, "cmptmp"), builder.getInt32Ty());
            if (op == ">=") return builder.CreateUIToFP(builder.CreateFCmpUGE(lhs, rhs, "cmptmp"), builder.getInt32Ty());
        } else {
            if (op == "==") return builder.CreateICmpEQ(lhs, rhs, "cmptmp");
            if (op == "!=") return builder.CreateICmpNE(lhs, rhs, "cmptmp");
            if (op == "<")  return builder.CreateICmpSLT(lhs, rhs, "cmptmp");
            if (op == "<=") return builder.CreateICmpSLE(lhs, rhs, "cmptmp");
            if (op == ">")  return builder.CreateICmpSGT(lhs, rhs, "cmptmp");
            if (op == ">=") return builder.CreateICmpSGE(lhs, rhs, "cmptmp");
        }
    }
    if (lhsIsFloat || rhsIsFloat) {
        if (op == "+") return builder.CreateFAdd(lhs, rhs, "faddtmp");
        if (op == "-") return builder.CreateFSub(lhs, rhs, "fsubtmp");
        if (op == "*") return builder.CreateFMul(lhs, rhs, "fmultmp");
        if (op == "/") return builder.CreateFDiv(lhs, rhs, "fdivtmp");
    } else {
        if (op == "+") return builder.CreateAdd(lhs, rhs, "addtmp");
        if (op == "-") return builder.CreateSub(lhs, rhs, "subtmp");
        if (op == "*") return builder.CreateMul(lhs, rhs, "multmp");
        if (op == "/") return builder.CreateSDiv(lhs, rhs, "divtmp");
    }
    throw std::string("Unknown operator: ") + op;
}

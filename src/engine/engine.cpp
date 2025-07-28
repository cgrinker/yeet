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
    // If result is int, cast to double before returning
    if (result->getType()->isIntegerTy()) {
        result = builder.CreateSIToFP(result, builder.getDoubleTy(), "intToDouble");
    }
    builder.CreateRet(result);

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
    auto calcFn = sym->toPtr<double(*)()>();
    double value = calcFn();
    std::cout << "JIT result: " << value << std::endl;
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
    auto it = symbolTable.find(node.value);
    if (it == symbolTable.end()) throw std::string("Unknown variable: ") + node.value;
    return builder.CreateLoad(builder.getInt32Ty(), it->second, node.value);
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
    if (op == "=") {
        // Assignment: (= var value)
        if (node.values.size() != 3) throw "Expected variable and value";
        const EdnNode& varNode = *(++node.values.begin());
        const EdnNode& valNode = *(++++node.values.begin());
        if (varNode.type != EdnSymbol) throw "Expected variable name";
        llvm::Value* value = this->codegenExpr(valNode, context, builder);
        llvm::Value* alloca = builder.CreateAlloca(builder.getInt32Ty(), nullptr, varNode.value);
        builder.CreateStore(value, alloca);
        symbolTable[varNode.value] = alloca;
        return value;
    }
    if (node.values.size() != 3) throw "Expected two operands";
    llvm::Value* lhs = this->codegenExpr(*(++node.values.begin()), context, builder);
    llvm::Value* rhs = this->codegenExpr(*(++++node.values.begin()), context, builder);

    // If either operand is float, do floating point arithmetic
    bool lhsIsFloat = (*(++node.values.begin())).type == EdnFloat;
    bool rhsIsFloat = (*(++++node.values.begin())).type == EdnFloat;
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
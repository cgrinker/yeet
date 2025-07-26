#include "engine.hpp"

using namespace yeet;

#include "../edn/edn.hpp"


// Symbol table for variable assignments (function-local, static for demo)
static std::unordered_map<std::string, llvm::Value*> symbolTable;

// Extended: supports (+ a b), (- a b), (* a b), (/ a b), (:= var value), and variable lookup
llvm::Value* codegenExpr(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    if (node.type == EdnInt) {
        return llvm::ConstantInt::get(builder.getInt32Ty(), std::stoi(node.value));
    }
    if (node.type == EdnSymbol) {
        // Variable lookup
        auto it = symbolTable.find(node.value);
        if (it == symbolTable.end()) throw std::string("Unknown variable: ") + node.value;
        return builder.CreateLoad(builder.getInt32Ty(), it->second, node.value);
    }
    if (node.type == EdnList && !node.values.empty()) {
        // If this is a sequence of expressions (not an operation), evaluate each and return the last
        bool allAreLists = true;
        for (const auto& v : node.values) {
            if (v.type != EdnList && v.type != EdnInt && v.type != EdnSymbol) {
                allAreLists = false;
                break;
            }
        }
        if (allAreLists && node.values.size() > 1 && node.values.front().type == EdnList) {
            llvm::Value* last = nullptr;
            for (const auto& expr : node.values) {
                last = codegenExpr(expr, context, builder);
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
            llvm::Value* value = codegenExpr(valNode, context, builder);
            llvm::Value* alloca = builder.CreateAlloca(builder.getInt32Ty(), nullptr, varNode.value);
            builder.CreateStore(value, alloca);
            symbolTable[varNode.value] = alloca;
            return value;
        }
        if (node.values.size() != 3) throw "Expected two operands";
        llvm::Value* lhs = codegenExpr(*(++node.values.begin()), context, builder);
        llvm::Value* rhs = codegenExpr(*(++++node.values.begin()), context, builder);
        if (op == "+") return builder.CreateAdd(lhs, rhs, "addtmp");
        if (op == "-") return builder.CreateSub(lhs, rhs, "subtmp");
        if (op == "*") return builder.CreateMul(lhs, rhs, "multmp");
        if (op == "/") return builder.CreateSDiv(lhs, rhs, "divtmp");
        throw std::string("Unknown operator: ") + op;
    }
    throw "Unsupported expression";
}


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

    _jit = std::move(*llvm::orc::LLJITBuilder().create());
    _context = std::make_unique<llvm::LLVMContext>();
}

void Engine::run(std::string& s)
{
    auto node = edn::read(s);
    auto module = std::make_unique<llvm::Module>("calc_module", *_context);
    llvm::IRBuilder<> builder(*_context);
    // Create function prototype: int calc()
    auto funcType = llvm::FunctionType::get(builder.getInt32Ty(), false);
    auto func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "calc", module.get());
    auto entry = llvm::BasicBlock::Create(*_context, "entry", func);
    builder.SetInsertPoint(entry);
    llvm::Value* result = nullptr;
    try {
        result = codegenExpr(node, *_context, builder);
    } catch (const char* msg) {
        std::cerr << "Codegen error: " << msg << std::endl;
        return;
    } catch (const std::string& msg) {
        std::cerr << "Codegen error: " << msg << std::endl;
        return;
    }
    builder.CreateRet(result);

    // Print the generated LLVM IR
    std::cout << "\n===== Generated LLVM IR =====\n";
    module->print(llvm::outs(), nullptr);
    std::cout << "\n============================\n" << std::endl;

    // Add module to JIT
    if (auto err = _jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(_context)))) {
        std::cerr << "Failed to add module to JIT: " << llvm::toString(std::move(err)) << std::endl;
        return;
    }
    // Look up and run the function
    auto sym = _jit->lookup("calc");
    if (!sym) {
        std::cerr << "Failed to find function: " << llvm::toString(sym.takeError()) << std::endl;
        return;
    }
    auto calcFn = sym->toPtr<int(*)()>();
    int value = calcFn();
    std::cout << "JIT result: " << value << std::endl;
}
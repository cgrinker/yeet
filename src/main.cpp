// #include <fmt/core.h>
#include <cxxopts.hpp>
#include <iostream>
#include "edn/edn.hpp"
#include <fstream>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/Support/TargetSelect.h>
#include <memory>

// Simple calculator: supports (+ a b), (- a b), (* a b), (/ a b)
llvm::Value* codegenExpr(const edn::EdnNode& node, llvm::LLVMContext& context, llvm::IRBuilder<>& builder) {
    using namespace edn;
    if (node.type == EdnInt) {
        return llvm::ConstantInt::get(builder.getInt32Ty(), std::stoi(node.value));
    }
    if (node.type == EdnList && !node.values.empty()) {
        const EdnNode& opNode = node.values.front();
        if (opNode.type != EdnSymbol) throw "Expected operator symbol";
        std::string op = opNode.value;
        if (node.values.size() != 3) throw "Expected two operands";
        llvm::Value* lhs = codegenExpr(*(++node.values.begin()), context, builder);
        llvm::Value* rhs = codegenExpr(*(++++node.values.begin()), context, builder);
        if (op == "+") return builder.CreateAdd(lhs, rhs, "addtmp");
        if (op == "-") return builder.CreateSub(lhs, rhs, "subtmp");
        if (op == "*") return builder.CreateMul(lhs, rhs, "multmp");
        if (op == "/") return builder.CreateSDiv(lhs, rhs, "divtmp");
        throw "Unknown operator: " + op;
    }
    throw "Unsupported expression";
}

int main(int argc, char *argv[])
{
    cxxopts::Options options("yeet", "I'm Finna yeet");
    options.add_options()("h,help", "Print usage")("f, filename", "The filename to execute", cxxopts::value<std::vector<std::string>>());
    ;

    try
    {
        auto result = options.parse(argc, argv);
        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (result.count("filename"))
        {
            auto filenames = result["filename"].as<std::vector<std::string>>();
            if (filenames.empty())
            {
                std::cerr << "No filename provided." << std::endl;
                return 1;
            }
            std::string filename = filenames[0];
            std::ifstream file(filename);
            if (!file)
            {
                std::cerr << "Failed to open file: " << filename << std::endl;
                return 1;
            }
            std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            try
            {
                auto node = edn::read(contents);
                llvm::InitializeNativeTarget();
                llvm::InitializeNativeTargetAsmPrinter();
                llvm::InitializeNativeTargetAsmParser();
                // LLVM JIT setup
                auto jit = llvm::orc::LLJITBuilder().create();
                if (!jit) {
                    std::cerr << "Failed to create LLVM JIT: " << llvm::toString(jit.takeError()) << std::endl;
                    return 1;
                }
                auto& lljit = *jit;
                auto context = std::make_unique<llvm::LLVMContext>();
                auto module = std::make_unique<llvm::Module>("calc_module", *context);
                llvm::IRBuilder<> builder(*context);
                // Create function prototype: int calc()
                auto funcType = llvm::FunctionType::get(builder.getInt32Ty(), false);
                auto func = llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, "calc", module.get());
                auto entry = llvm::BasicBlock::Create(*context, "entry", func);
                builder.SetInsertPoint(entry);
                llvm::Value* result = nullptr;
                try {
                    result = codegenExpr(node, *context, builder);
                } catch (const char* msg) {
                    std::cerr << "Codegen error: " << msg << std::endl;
                    return 1;
                } catch (const std::string& msg) {
                    std::cerr << "Codegen error: " << msg << std::endl;
                    return 1;
                }
                builder.CreateRet(result);
                // Add module to JIT
                if (auto err = lljit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
                    std::cerr << "Failed to add module to JIT: " << llvm::toString(std::move(err)) << std::endl;
                    return 1;
                }
                // Look up and run the function
                auto sym = lljit->lookup("calc");
                if (!sym) {
                    std::cerr << "Failed to find function: " << llvm::toString(sym.takeError()) << std::endl;
                    return 1;
                }
                auto calcFn = sym->toPtr<int(*)()>();
                int value = calcFn();
                std::cout << "JIT result: " << value << std::endl;
            }
            catch (const std::exception &e)
            {
                std::cerr << "EDN parse error: " << e.what() << std::endl;
                return 1;
            }
            catch (const char *msg)
            {
                std::cerr << "EDN parse error: " << msg << std::endl;
                return 1;
            }
        }
        else
        {
            std::cerr << "No filename provided." << std::endl;
            return 1;
        }
    }
    catch (const cxxopts::exceptions::exception &e)
    {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

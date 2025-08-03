
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

std::ostream& operator<<(std::ostream& stream, const llvm::Type& type) {
    std::string typeStr;
    if (type.isIntegerTy()) typeStr = fmt::format("int{}", type.getIntegerBitWidth());
    else if (type.isFloatTy()) typeStr = "float";
    else if (type.isDoubleTy()) typeStr = "double";
    else if (type.isVoidTy()) typeStr = "void";
    else if (type.isPointerTy()) typeStr = "ptr"; // Opaque pointer, no element type
    else if (type.isStructTy()) typeStr = fmt::format("struct {}", type.getStructName().str());
    else typeStr = type.getStructName().str().empty() ? "unknown" : type.getStructName().str();
    stream << "<LLVMType: " << typeStr << ">";
    return stream;
}

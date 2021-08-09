#include <sstream>

#include "exceptions.h"

namespace Exceptions {

NameAlreadyUsedException::NameAlreadyUsedException(std::string const& name) {
    std::ostringstream err;
    err << "Name " << name << " already defined" << std::endl;

    _error = err.str();
}

BadTypeException::BadTypeException(std::string const& expected, std::string const& received, std::string const& context) {
    std::ostringstream err;
    err << "Bad type received " << context << ": expected " << expected << ", got " << received << std::endl;
    _error = err.str();
}

ContextlessBadTypeException::ContextlessBadTypeException(std::string const& expected, std::string const& received) : BadTypeException(expected, received, "") {

}

ValueEqualityExpected::ValueEqualityExpected(const std::string& expression, std::string const& expected, std::string const& received) {
    _error = "Expression " + expression + " has value " + received + ", expected " + expected + "\n";
}

TypeEqualityExpected::TypeEqualityExpected(const std::string& expression, const std::string& expected, const std::string& received) {
    std::ostringstream error;
    error << "Expression " << expression << " has type " << received << ", expected " << expected << std::endl;
    _error = error.str();
}

NilAccess::NilAccess(std::string const& detail) {
    _error = detail + " on nil value";
}

NilDot::NilDot() : NilAccess("Attempt to use dot") { }

BadDotAccess::BadDotAccess(std::string const& type) {
    _error = "Attempt to use dot on " + type;
}

CrossedLocal::CrossedLocal(std::string const& label, std::vector<std::string> const& locals) {
    _error = "goto " + label + " crosses initialization of local" + ((locals.size() != 1) ? "s" : "") + ": ";
    for (std::string const& local: locals) {
        _error += local + " ";
    }
}

InvisibleLabel::InvisibleLabel(std::string const& label) {
    _error = "Label "+ label + " is not visible\n";
}

LonelyBreak::LonelyBreak(int line) {
    std::ostringstream error;
    error << "Lonely break found on line " << line  << "\n";
    _error = error.str();
}

LabelAlreadyDefined::LabelAlreadyDefined(std::string const& label) {
    _error = "Label " + label + " already defined\n";
}

StackCorruption::StackCorruption(int expected, int received) {
    std::ostringstream error;
    error << "Stack corruption detected: expected at least " << expected << " frames, got " << received << std::endl;
    _error = error.str();
}

BadCall::BadCall(std::string const& type) {
    _error = "Attempted to call a function on " + type;
}

}

#include "pathvalidator.hpp"
#include <algorithm>
#include <sstream>
#include <filesystem>

namespace Kitaplik::Core {

// Static member definitions
const std::vector<char> PathValidator::INVALID_FILENAME_CHARS = {
    '\0', '<', '>', ':', '"', '|', '?', '*'
};

const std::vector<std::string> PathValidator::DANGEROUS_COMPONENTS = {
    "..", ".", "~", "CON", "PRN", "AUX", "NUL",
    "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
    "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
};

Result<std::string> PathValidator::validatePath(const std::string& path) {
    if (path.empty()) {
        return Result<std::string>(FileError::InvalidPath, "Empty path");
    }

    if (!isPathLengthValid(path)) {
        return Result<std::string>(FileError::InvalidPath, "Path too long");
    }

    if (containsNullBytes(path)) {
        return Result<std::string>(FileError::InvalidPath, "Path contains null bytes");
    }

    if (containsInvalidChars(path)) {
        return Result<std::string>(FileError::InvalidPath, "Path contains invalid characters");
    }

    if (hasPathTraversal(path)) {
        return Result<std::string>(FileError::InvalidPath, "Path contains traversal attempts");
    }

    if (containsDangerousComponents(path)) {
        return Result<std::string>(FileError::InvalidPath, "Path contains dangerous components");
    }

    try {
        std::string normalized = normalizePath(path);
        return Result<std::string>(normalized);
    } catch (const std::exception& e) {
        return Result<std::string>(FileError::InvalidPath, "Failed to normalize path", e.what());
    }
}

std::string PathValidator::sanitizePath(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    std::string result = path;
    
    // Remove null bytes
    result.erase(std::remove(result.begin(), result.end(), '\0'), result.end());
    
    // Replace dangerous components
    std::istringstream iss(result);
    std::ostringstream oss;
    std::string component;
    bool first = true;
    
    while (std::getline(iss, component, '/')) {
        if (component.empty()) continue;
        
        // Skip dangerous components
        if (std::find(DANGEROUS_COMPONENTS.begin(), DANGEROUS_COMPONENTS.end(), component) != DANGEROUS_COMPONENTS.end()) {
            continue;
        }
        
        if (!first) {
            oss << '/';
        }
        oss << component;
        first = false;
    }
    
    return normalizePath(oss.str());
}

bool PathValidator::isPathSafe(const std::string& path) {
    return validatePath(path).isSuccess();
}

std::string PathValidator::normalizePath(const std::string& path) {
    try {
        std::filesystem::path fsPath(path);
        std::filesystem::path normalized = std::filesystem::absolute(fsPath).lexically_normal();
        return normalized.string();
    } catch (const std::exception&) {
        // Fallback to manual normalization if filesystem fails
        std::vector<std::string> components;
        std::istringstream iss(path);
        std::string component;
        
        while (std::getline(iss, component, '/')) {
            if (component.empty() || component == ".") {
                continue;
            } else if (component == "..") {
                if (!components.empty()) {
                    components.pop_back();
                }
            } else {
                components.push_back(component);
            }
        }
        
        std::ostringstream oss;
        for (size_t i = 0; i < components.size(); ++i) {
            if (i > 0) {
                oss << '/';
            }
            oss << components[i];
        }
        
        return oss.str();
    }
}

Result<std::string> PathValidator::validatePathWithinBase(const std::string& path, const std::string& baseDir) {
    auto pathResult = validatePath(path);
    if (!pathResult.isSuccess()) {
        return pathResult;
    }

    auto baseResult = validatePath(baseDir);
    if (!baseResult.isSuccess()) {
        return Result<std::string>(FileError::InvalidPath, "Invalid base directory");
    }

    try {
        std::filesystem::path fsPath(pathResult.value());
        std::filesystem::path fsBase(baseResult.value());
        
        std::filesystem::path absPath = std::filesystem::absolute(fsPath);
        std::filesystem::path absBase = std::filesystem::absolute(fsBase);
        
        // Check if path is within base directory
        auto rel = std::filesystem::relative(absPath, absBase);
        if (rel.begin() == rel.end() || *rel.begin() == "..") {
            return Result<std::string>(FileError::InvalidPath, "Path is outside base directory");
        }
        
        return Result<std::string>(absPath.string());
    } catch (const std::exception& e) {
        return Result<std::string>(FileError::InvalidPath, "Failed to validate path within base", e.what());
    }
}

bool PathValidator::hasPathTraversal(const std::string& path) {
    return path.find("../") != std::string::npos || 
           path.find("..\\") != std::string::npos ||
           path.find("..") == 0;  // Starts with ..
}

Result<std::string> PathValidator::validateFileName(const std::string& filename) {
    if (filename.empty()) {
        return Result<std::string>(FileError::InvalidPath, "Empty filename");
    }

    if (filename.length() > 255) {
        return Result<std::string>(FileError::InvalidPath, "Filename too long");
    }

    if (containsNullBytes(filename)) {
        return Result<std::string>(FileError::InvalidPath, "Filename contains null bytes");
    }

    // Check for invalid characters
    for (char c : INVALID_FILENAME_CHARS) {
        if (filename.find(c) != std::string::npos) {
            return Result<std::string>(FileError::InvalidPath, "Filename contains invalid characters");
        }
    }

    // Check for dangerous names
    if (std::find(DANGEROUS_COMPONENTS.begin(), DANGEROUS_COMPONENTS.end(), filename) != DANGEROUS_COMPONENTS.end()) {
        return Result<std::string>(FileError::InvalidPath, "Filename is reserved");
    }

    // Check for leading/trailing spaces and dots
    if (filename.front() == ' ' || filename.front() == '.' ||
        filename.back() == ' ' || filename.back() == '.') {
        return Result<std::string>(FileError::InvalidPath, "Filename has invalid leading/trailing characters");
    }

    return Result<std::string>(filename);
}

bool PathValidator::containsInvalidChars(const std::string& path) {
    // Check for control characters (except newline and tab which might be valid in some contexts)
    for (char c : path) {
        if (c >= 0 && c < 32 && c != '\n' && c != '\t') {
            return true;
        }
    }
    return false;
}

bool PathValidator::isPathLengthValid(const std::string& path) {
    return path.length() <= MAX_PATH_LENGTH;
}

bool PathValidator::containsNullBytes(const std::string& path) {
    return path.find('\0') != std::string::npos;
}

bool PathValidator::containsDangerousComponents(const std::string& path) {
    std::istringstream iss(path);
    std::string component;
    
    while (std::getline(iss, component, '/')) {
        if (!component.empty() && 
            std::find(DANGEROUS_COMPONENTS.begin(), DANGEROUS_COMPONENTS.end(), component) != DANGEROUS_COMPONENTS.end()) {
            return true;
        }
    }
    return false;
}

} // namespace Kitaplik::Core
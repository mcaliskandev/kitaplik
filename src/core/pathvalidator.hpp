#ifndef PATHVALIDATOR_HPP
#define PATHVALIDATOR_HPP

#include <string>
#include <vector>
#include <optional>
#include "errors/fileerror.hpp"

namespace Kitaplik::Core {

/**
 * @brief Validates and sanitizes file system paths for security
 */
class PathValidator {
public:
    /**
     * @brief Validates if a path is safe for file operations
     * @param path The path to validate
     * @return Result containing the normalized path if valid, or error if invalid
     */
    static Result<std::string> validatePath(const std::string& path);

    /**
     * @brief Sanitizes a path by removing dangerous components
     * @param path The path to sanitize
     * @return Sanitized path
     */
    static std::string sanitizePath(const std::string& path);

    /**
     * @brief Checks if a path contains dangerous components
     * @param path The path to check
     * @return true if path is safe, false otherwise
     */
    static bool isPathSafe(const std::string& path);

    /**
     * @brief Normalizes a path (resolves "..", ".", redundant separators)
     * @param path The path to normalize
     * @return Normalized path
     */
    static std::string normalizePath(const std::string& path);

    /**
     * @brief Validates that a path is within a given base directory
     * @param path The path to validate
     * @param baseDir The base directory that path must be within
     * @return Result containing the validated path if safe, or error if not
     */
    static Result<std::string> validatePathWithinBase(const std::string& path, const std::string& baseDir);

    /**
     * @brief Checks for path traversal attempts
     * @param path The path to check
     * @return true if path traversal is detected, false otherwise
     */
    static bool hasPathTraversal(const std::string& path);

    /**
     * @brief Validates file name for security
     * @param filename The filename to validate
     * @return Result containing the validated filename if safe, or error if not
     */
    static Result<std::string> validateFileName(const std::string& filename);

private:
    // Maximum path length to prevent buffer overflow attacks
    static constexpr size_t MAX_PATH_LENGTH = 4096;
    
    // Characters not allowed in filenames
    static const std::vector<char> INVALID_FILENAME_CHARS;
    
    // Dangerous path components
    static const std::vector<std::string> DANGEROUS_COMPONENTS;
    
    static bool containsInvalidChars(const std::string& path);
    static bool isPathLengthValid(const std::string& path);
    static bool containsNullBytes(const std::string& path);
    static bool containsDangerousComponents(const std::string& path);
};

} // namespace Kitaplik::Core

#endif // PATHVALIDATOR_HPP
#include "StringMatcher.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <climits>
#include <sstream>
#include <iomanip>

int StringMatcher::damerauLevenshteinDistance(const std::string& s1, const std::string& s2) {
    size_t len1 = s1.length();
    size_t len2 = s2.length();
    
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    
    std::vector<std::vector<int>> d(len1 + 1, std::vector<int>(len2 + 1));
    
    for (size_t i = 0; i <= len1; i++) d[i][0] = i;
    for (size_t j = 0; j <= len2; j++) d[0][j] = j;
    
    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            
            d[i][j] = std::min({
                d[i-1][j] + 1,
                d[i][j-1] + 1,
                d[i-1][j-1] + cost
            });
            
            if (i > 1 && j > 1 && s1[i-1] == s2[j-2] && s1[i-2] == s2[j-1]) {
                d[i][j] = std::min(d[i][j], d[i-2][j-2] + cost);
            }
        }
    }
    
    return d[len1][len2];
}

std::string StringMatcher::removeExtension(const std::string& filename) {
    std::vector<std::string> extensions = {".sfc", ".smc", ".zip", ".fig", ".bin", ".png"};
    std::string result = filename;
    
    for (const auto& ext : extensions) {
        if (result.length() > ext.length()) {
            std::string ending = result.substr(result.length() - ext.length());
            std::string lowerEnding = toLowercase(ending);
            if (lowerEnding == ext) {
                result = result.substr(0, result.length() - ext.length());
                break;
            }
        }
    }
    
    return result;
}

std::string StringMatcher::removeRegionCodes(const std::string& name) {
    std::string result = name;
    
    std::vector<std::regex> patterns = {
        std::regex("\\s*\\([^)]*\\)\\s*"),
        std::regex("\\s*\\[[^\\]]*\\]\\s*")
    };
    
    for (const auto& pattern : patterns) {
        result = std::regex_replace(result, pattern, " ");
    }
    
    return result;
}

std::string StringMatcher::toLowercase(const std::string& input) {
    std::string result = input;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string StringMatcher::removeSpecialChars(const std::string& input) {
    std::string result;
    result.reserve(input.length());
    
    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ') {
            result += c;
        } else if (c == '-' || c == '_' || c == ':' || c == '\'') {
            result += ' ';
        }
    }
    
    return result;
}

std::string StringMatcher::trim(const std::string& input) {
    size_t start = input.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    
    size_t end = input.find_last_not_of(" \t\n\r");
    std::string result = input.substr(start, end - start + 1);
    
    std::string cleaned;
    bool lastWasSpace = false;
    for (char c : result) {
        if (c == ' ') {
            if (!lastWasSpace) {
                cleaned += c;
                lastWasSpace = true;
            }
        } else {
            cleaned += c;
            lastWasSpace = false;
        }
    }
    
    return cleaned;
}

std::string StringMatcher::normalize(const std::string& input) {
    std::string result = removeExtension(input);
    result = removeRegionCodes(result);
    result = toLowercase(result);
    result = removeSpecialChars(result);
    result = trim(result);
    return result;
}

std::string StringMatcher::findBestMatch(const std::string& romName, const std::vector<std::string>& candidates) {
    if (candidates.empty()) return "";
    
    std::string normalizedRom = normalize(romName);
    
    std::string bestMatch;
    int bestDistance = INT_MAX;
    
    for (const auto& candidate : candidates) {
        std::string normalizedCandidate = normalize(candidate);
        int distance = damerauLevenshteinDistance(normalizedRom, normalizedCandidate);
        
        if (distance < bestDistance) {
            bestDistance = distance;
            bestMatch = candidate;
        }
        
        if (distance == 0) break;
    }
    
    return bestMatch;
}

std::string StringMatcher::urlEncode(const std::string& input) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~' || c == '(' || c == ')' || c == '!' || c == '\'') {
            escaped << c;
        } else if (c == ' ') {
            escaped << "%20";
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    
    return escaped.str();
}

std::string StringMatcher::urlDecode(const std::string& input) {
    std::string output = "";
    for (size_t i = 0; i < input.length(); i++) {
        if (input[i] == '%' && i + 2 < input.length()) {
            unsigned int value;
            if (sscanf(input.substr(i + 1, 2).c_str(), "%x", &value) == 1) {
                output += static_cast<char>(value);
                i += 2;
            } else {
                output += input[i];
            }
        } else if (input[i] == '+') {
            output += ' ';
        } else {
            output += input[i];
        }
    }
    return output;
}

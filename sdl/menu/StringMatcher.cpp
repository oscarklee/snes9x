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
    
    if (len1 > 128 || len2 > 128) return 999;

    // Use a fixed-size buffer on the stack to avoid malloc thrashing
    int d[130][130];
    
    for (size_t i = 0; i <= len1; i++) d[i][0] = i;
    for (size_t j = 0; j <= len2; j++) d[0][j] = j;
    
    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            
            int del = d[i-1][j] + 1;
            int ins = d[i][j-1] + 1;
            int sub = d[i-1][j-1] + cost;
            
            int res = std::min({del, ins, sub});
            
            if (i > 1 && j > 1 && s1[i-1] == s2[j-2] && s1[i-2] == s2[j-1]) {
                res = std::min(res, d[i-2][j-2] + cost);
            }
            d[i][j] = res;
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
    std::string result = "";
    int parenLevel = 0;
    int bracketLevel = 0;
    
    for (char c : name) {
        if (c == '(') parenLevel++;
        else if (c == ')') parenLevel--;
        else if (c == '[') bracketLevel++;
        else if (c == ']') bracketLevel--;
        else if (parenLevel == 0 && bracketLevel == 0) {
            result += c;
        }
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
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '+' || c == '&') {
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

std::string StringMatcher::findBestMatchFast(const std::string& normalizedRom, const std::vector<std::string>& candidates, const std::vector<std::string>& normalizedCandidates) {
    if (candidates.empty() || candidates.size() != normalizedCandidates.size()) return "";
    
    std::string bestMatch;
    int bestDistance = INT_MAX;
    
    for (size_t i = 0; i < candidates.size(); i++) {
        int distance = damerauLevenshteinDistance(normalizedRom, normalizedCandidates[i]);
        
        if (distance < bestDistance) {
            bestDistance = distance;
            bestMatch = candidates[i];
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
    std::string output;
    output.reserve(input.length());
    for (size_t i = 0; i < input.length(); i++) {
        if (input[i] == '%' && i + 2 < input.length()) {
            char hex[3] = { input[i+1], input[i+2], 0 };
            char* end;
            int val = strtol(hex, &end, 16);
            if (*end == 0) {
                output += (char)val;
                i += 2;
            } else {
                output += '%';
            }
        } else if (input[i] == '+') {
            output += ' ';
        } else {
            output += input[i];
        }
    }
    return output;
}

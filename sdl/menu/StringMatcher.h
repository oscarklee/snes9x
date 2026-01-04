#ifndef STRING_MATCHER_H
#define STRING_MATCHER_H

#include <string>
#include <vector>

class StringMatcher {
public:
    static int damerauLevenshteinDistance(const std::string& s1, const std::string& s2);
    static std::string normalize(const std::string& input);
    static std::string findBestMatch(const std::string& romName, const std::vector<std::string>& candidates);
    static std::string urlEncode(const std::string& input);
    static std::string urlDecode(const std::string& input);

private:
    static std::string removeExtension(const std::string& filename);
    static std::string removeRegionCodes(const std::string& name);
    static std::string toLowercase(const std::string& input);
    static std::string removeSpecialChars(const std::string& input);
    static std::string trim(const std::string& input);
};

#endif


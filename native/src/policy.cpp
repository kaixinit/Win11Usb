#include "win11usb.hpp"
#include <algorithm>
#include <regex>

namespace wu {
namespace {
struct Hint {
    std::set<std::wstring> families, releases;
    bool ltsc = false, standard = false;
};
Hint Hints(const std::wstring &text, bool ltsc) {
    Hint h;
    auto s = Lower(text);
    std::wregex family(L"windows[ _-]+(10|11)(?![0-9])"), lt(L"(^|[^a-z0-9])(ltsc|ltsb)(?![a-z])"),
        standard(L"(^|[^a-z0-9])([0-9]{2}h[12])(?![a-z0-9])"),
        year(L"(?:ltsc|ltsb)[ _-]*(20[0-9]{2})(?![0-9])");
    for (std::wsregex_iterator i(s.begin(), s.end(), family), end; i != end; ++i)
        h.families.insert((*i)[1]);
    h.ltsc = std::regex_search(s, lt);
    h.standard = std::regex_search(s, standard);
    const auto &re = ltsc ? year : standard;
    for (std::wsregex_iterator i(s.begin(), s.end(), re), end; i != end; ++i) {
        std::wstring r = (*i)[ltsc ? 1 : 2];
        for (auto &c : r)
            c = towupper(c);
        h.releases.insert(r);
    }
    return h;
}
const std::map<std::wstring, std::vector<int>> Rules = {{L"10:LTSC:2015", {10240}},
                                                        {L"10:LTSC:2016", {14393}},
                                                        {L"10:LTSC:2019", {17763}},
                                                        {L"10:LTSC:2021", {19041, 19044}},
                                                        {L"11:LTSC:2024", {26100}},
                                                        {L"10:Standard:20H2", {19041, 19042}},
                                                        {L"10:Standard:21H1", {19041, 19043}},
                                                        {L"10:Standard:21H2", {19041, 19044}},
                                                        {L"10:Standard:22H2", {19041, 19045}},
                                                        {L"11:Standard:21H2", {22000}},
                                                        {L"11:Standard:22H2", {22621}},
                                                        {L"11:Standard:23H2", {22621, 22631}},
                                                        {L"11:Standard:24H2", {26100}},
                                                        {L"11:Standard:25H2", {26100, 26200}},
                                                        {L"11:Standard:26H1", {28000}},
                                                        {L"11:Standard:26H2", {26300}}};
bool Starts(const std::wstring &a, const std::wstring &b) {
    return a.size() >= b.size() && Lower(a.substr(0, b.size())) == Lower(b);
}
} // namespace
ImageIdentity ResolveIdentity(ImageIdentity d, const std::wstring &iso) {
    auto n = Lower(d.name), ed = Lower(d.edition), inst = Lower(d.installation), prod = Lower(d.product);
    bool server = Starts(inst, L"server") || prod == L"servernt" || prod == L"lanmannt" ||
                  Starts(ed, L"server") || n.find(L"windows server") != n.npos;
    bool client = inst == L"client" || prod == L"winnt" ||
                  std::regex_search(n, std::wregex(L"\\bwindows\\s+(10|11)\\b"));
    d.ltsc = std::regex_search(Lower(d.name + L" " + d.description), std::wregex(L"\\b(ltsc|ltsb)\\b")) ||
             std::regex_match(ed, std::wregex(L"(enterprises|enterprisesn|iotenterprises)(eval)?"));
    d.status = L"Unknown";
    d.note = L"发行版本未确认，保留基础卷标。";
    if (server) {
        d.display = n.find(L"windows server") != n.npos ? d.name : L"Windows Server";
        d.label = L"WINSERVER";
        return d;
    }
    if (!client || d.major != 10 || d.build < 10240)
        return d;
    std::wstring family = d.build >= 22000 ? L"11" : L"10", prefix = L"Windows " + family;
    d.label = L"WIN" + family;
    d.display = std::regex_replace(d.name, std::wregex(L"^Windows\\s+(10|11)\\b", std::regex::icase), prefix);
    if (!Starts(d.display, prefix))
        d.display = prefix;
    if (d.ltsc) {
        d.label += L"LTSC";
        if (!std::regex_search(Lower(d.display), std::wregex(L"\\b(ltsc|ltsb)\\b")))
            d.display += L" LTSC";
    }
    std::wstring name = BaseName(iso);
    auto dot = name.find_last_of(L'.');
    if (dot != name.npos)
        name.resize(dot);
    Hint m = Hints(d.name + L" " + d.description, d.ltsc), f = Hints(name, d.ltsc);
    bool conflict = m.releases.size() > 1 || f.releases.size() > 1 || f.families.size() > 1;
    if (f.families.size() == 1 && *f.families.begin() != family)
        conflict = true;
    if (f.families.size() == 1 && (f.ltsc || !f.releases.empty() || f.standard) && f.ltsc != d.ltsc)
        conflict = true;
    std::wstring mr = m.releases.size() == 1 ? *m.releases.begin() : L"",
                 fr = f.families.size() == 1 && f.releases.size() == 1 ? *f.releases.begin() : L"";
    if (!mr.empty() && !fr.empty() && mr != fr)
        conflict = true;
    std::wstring group = family + L":" + (d.ltsc ? L"LTSC:" : L"Standard:");
    for (auto r : {mr, fr})
        if (!r.empty()) {
            auto rule = Rules.find(group + r);
            if (rule == Rules.end() ||
                std::find(rule->second.begin(), rule->second.end(), d.build) == rule->second.end())
                conflict = true;
        }
    if (conflict) {
        d.status = L"Conflict";
        d.note = L"映像信息与 ISO 名称或版本规则不一致，发行版本未确认，保留基础卷标。";
        return d;
    }
    if (!mr.empty()) {
        d.release = mr;
        d.status = L"ImageMetadata";
    } else if (!fr.empty()) {
        d.release = fr;
        d.status = L"IsoNameHint";
    } else {
        std::vector<std::wstring> matches;
        for (auto &r : Rules)
            if (Starts(r.first, group) &&
                std::find(r.second.begin(), r.second.end(), d.build) != r.second.end())
                matches.push_back(r.first.substr(group.size()));
        if (matches.size() == 1) {
            d.release = matches[0];
            d.status = L"BuildRule";
        }
    }
    if (!d.release.empty()) {
        if (!d.ltsc)
            d.label += L"_" + d.release;
        if (Lower(d.display).find(Lower(d.release)) == d.display.npos)
            d.display += L" " + d.release;
        d.note = d.status == L"IsoNameHint"
                     ? L"发行版本依据 ISO 文件名辅助识别，已核对映像系统类型及兼容构建号；不能检测同系列 ISO "
                       L"被错误重命名的情况。"
                     : L"发行版本依据映像名称或兼容构建号与版本类型识别。";
    }
    return d;
}
void CheckSwm(const std::vector<File> &files) {
    if (files.empty())
        throw Error(L"拆分结果为空。");
    std::set<int> numbers;
    std::wregex pattern(L"install([2-9]|[1-9][0-9]+)\\.swm", std::regex::icase);
    for (auto &f : files) {
        std::wstring n = BaseName(f.full);
        if (n.empty())
            n = BaseName(f.relative);
        int number = 0;
        if (Lower(n) == L"install.swm")
            number = 1;
        else {
            std::wsmatch m;
            if (!std::regex_match(n, m, pattern))
                throw Error(L"SWM 分卷文件名无效。");
            try {
                number = std::stoi(m[1]);
            } catch (...) {
                throw Error(L"SWM 分卷编号无效。");
            }
        }
        if (f.size == 0 || f.size > Fat32Limit || !numbers.insert(number).second)
            throw Error(L"SWM 大小无效或分卷重复。");
    }
    int next = 1;
    for (int n : numbers)
        if (n != next++)
            throw Error(L"SWM 分卷不连续或不完整。");
}
ImagePlan PlanImages(const std::wstring &root, const std::vector<File> &files) {
    ImagePlan p;
    p.files = files;
    const File *wim = nullptr, *esd = nullptr, *swm = nullptr;
    std::set<std::wstring> unique;
    for (auto &f : p.files) {
        std::wstring r = Lower(f.relative);
        std::replace(r.begin(), r.end(), L'/', L'\\');
        if (r.empty() || r[0] == L'\\' || r.find(L':') != r.npos || r == L".." || r.find(L"..\\") != r.npos ||
            !unique.insert(r).second)
            throw Error(L"源文件相对路径无效或重复。");
        if (UINT64_MAX - p.total < f.size)
            throw Error(L"源文件总大小溢出。");
        p.total += f.size;
        if (r == L"sources\\install.wim")
            wim = &f;
        else if (r == L"sources\\install.esd")
            esd = &f;
        else if (r == L"sources\\install.swm")
            swm = &f;
    }
    if ((wim ? 1 : 0) + (esd ? 1 : 0) + (swm ? 1 : 0) != 1)
        throw Error(L"安装映像缺失或同时包含多种安装映像，尚未清盘。");
    if (wim || esd)
        for (const auto &f : p.files) {
            auto name = Lower(BaseName(f.relative));
            if (Lower(Parent(f.relative)) == L"sources" && Starts(name, L"install") && name.size() > 4 &&
                name.substr(name.size() - 4) == L".swm")
                throw Error(L"源介质混合了 WIM/ESD 与 SWM 分卷，尚未清盘。");
        }
    p.split = wim && wim->size > Fat32Limit;
    p.wim = root + L"sources\\install.wim";
    for (auto &f : p.files)
        if (f.size > Fat32Limit && !(p.split && Lower(f.relative) == L"sources\\install.wim"))
            throw Error(L"源文件超过 FAT32 单文件限制：" + f.relative);
    if (wim)
        p.images.push_back(*wim);
    else if (esd)
        p.images.push_back(*esd);
    else {
        for (auto &f : p.files)
            if (Lower(Parent(f.relative)) == L"sources" && Starts(Lower(BaseName(f.relative)), L"install") &&
                Lower(f.relative).size() >= 4 && Lower(f.relative).substr(f.relative.size() - 4) == L".swm")
                p.images.push_back(f);
        CheckSwm(p.images);
    }
    for (auto &f : p.images)
        if (f.size == 0)
            throw Error(L"安装映像为空，尚未清盘。");
    return p;
}
} // namespace wu

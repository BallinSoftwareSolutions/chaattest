#include "experience.h"
#include <iostream>
#include <stdexcept>
int main() {
  try {
    auto require = [](bool ok, const char *message) {
      if (!ok)
        throw std::runtime_error(message);
    };
    using namespace experience;
    require(status({true, true, false}) == Status::AlreadySet,
            "existing setting falsely claimed as app change");
    require(status({true, true, true}) == Status::Applied, "saved original status lost");
    require(status({true, false, true}) == Status::Changed, "external change hidden");
    auto selected = plan(
        {{true, false, false}, {true, true, false}, {false, false, false}, {true, false, true}});
    require(selected == std::vector<size_t>({0, 3}),
            "profile included unavailable or redundant changes");
    require(validResults({L"144.5", L"90", L"165,5", L"100"}), "decimal measurements rejected");
    for (auto bad : {L"", L"0", L"-1", L"nan", L"1e3", L"12..4", L"10001", L"9 FPS"})
      require(!fps(bad), "invalid measurement accepted");
    require(!validResults({L"60", L"90", L"120", L"80"}), "impossible 1% low accepted");
    require(std::abs(delta(100, 90) + 10) < .0001, "regression displayed as improvement");
    auto games = recent({L"a", L"b", L"c", L"d", L"e", L"f"}, L"b");
    require(games.size() == 6 && games.front() == L"b" && games[2] == L"c",
            "recent games duplicated or reordered incorrectly");
    require(recent(games, L"g").back() == L"e", "recent games limit not enforced");
    require(key(L"a") != key(L"b") && key(L"a") == L"12638187200555641996",
            "per-game key is not stable");
    std::cout << "PASS: truthful states, non-redundant profile plans, measured-result validation "
                 "and per-game identity\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <atomic>
#include <iostream>
#include "../../../src/app/Bootstrap/version_update.cpp"
#include "../../../src/app/Bootstrap/startup_update.cpp"

const char* app::localization::Get(const char* key) noexcept { return key; }
std::string app::localization::GetCopy(std::string_view key) { return std::string(key); }

namespace {
    int failures = 0;
    void Check(bool value, const char* expression, int line) {
        if (!value) { ++failures; std::cerr << line << ": " << expression << '\n'; }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    template<class Predicate> bool Wait(Predicate predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        CHECK(false);
        return false;
    }

    std::string Body(const std::string& tag) {
        return nlohmann::json{{"tag_name", tag}, {"html_url", "https://github.com/cs2-dma/CS2-DMA/releases/tag/" + tag},
            {"draft", false}, {"prerelease", false}}.dump();
    }

    struct HttpFixture {
        SOCKET listener = INVALID_SOCKET;
        uint16_t port = 0;
        std::string request;
        std::jthread worker;
        HttpFixture(std::string payload, bool stall = false) {
            listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            CHECK(listener != INVALID_SOCKET);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            CHECK(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
            CHECK(listen(listener, 1) == 0);
            int length = sizeof(address);
            CHECK(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0);
            port = ntohs(address.sin_port);
            worker = std::jthread([this, payload = std::move(payload), stall](std::stop_token stop) {
                while (!stop.stop_requested()) {
                    fd_set ready; FD_ZERO(&ready); FD_SET(listener, &ready);
                    timeval timeout{0, 10000};
                    if (select(0, &ready, nullptr, nullptr, &timeout) <= 0) continue;
                    const SOCKET client = accept(listener, nullptr, nullptr);
                    if (client == INVALID_SOCKET) return;
                    DWORD timeoutMs = 3000;
                    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
                    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
                    char buffer[4096];
                    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
                        const int received = recv(client, buffer, sizeof(buffer), 0);
                        if (received <= 0) break;
                        request.append(buffer, static_cast<size_t>(received));
                    }
                    size_t offset = 0;
                    while (offset < payload.size() && !stop.stop_requested()) {
                        const int sent = send(client, payload.data() + offset,
                            static_cast<int>(std::min<size_t>(8192, payload.size() - offset)), 0);
                        if (sent <= 0) break;
                        offset += static_cast<size_t>(sent);
                    }
                    if (stall) {
                        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                        while (!stop.stop_requested() && std::chrono::steady_clock::now() < until)
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    shutdown(client, SD_BOTH);
                    closesocket(client);
                    return;
                }
            });
        }
        ~HttpFixture() {
            worker.request_stop();
            if (worker.joinable()) worker.join();
            closesocket(listener);
        }
    };

    std::string Http(int status, const std::string& body, const std::string& headers = {}) {
        return "HTTP/1.1 " + std::to_string(status) + " Test\r\nConnection: close\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\n" + headers + "\r\n" + body;
    }

    void TestPolicies() {
        using namespace app::updates::policy;
        CHECK(ParseVersion("v1.2.3") == ParseVersion("1.2.3.0"));
        CHECK(*ParseVersion("v1.10.0") > *ParseVersion("v1.9.99"));
        for (const char* invalid : {"", "v", "1.2", "1.2.3.", "1.2.3.4.5", "01.2.3", "1.2.-3", "1.2.3-beta", "v1.2.4294967296"})
            CHECK(!ParseVersion(invalid));
        CHECK(ParseRelease(Body("v1.10.0"), "v1.9.0")->newer);
        CHECK(!ParseRelease(Body("v1.0.6"), "v1.0.6")->newer);
        CHECK(!ParseRelease(Body("v1.0.5"), "v1.0.6")->newer);
        CHECK(!ParseRelease("{invalid}", "v1.0.6"));
        auto payload = nlohmann::json::parse(Body("v1.0.7"));
        payload["prerelease"] = true;
        CHECK(!ParseRelease(payload.dump(), "v1.0.6"));
        payload["prerelease"] = false;
        payload["draft"] = true;
        CHECK(!ParseRelease(payload.dump(), "v1.0.6"));
        payload["draft"] = false;
        payload["html_url"] = "https://github.com.evil.example/releases/v1.0.7";
        CHECK(!ParseRelease(payload.dump(), "v1.0.6"));
        payload["html_url"] = "https://github.com/cs2-dma/CS2-DMA/releases/tag/v1.0.7\r\n";
        CHECK(!ParseRelease(payload.dump(), "v1.0.6"));
        CHECK(!ParseRelease(std::string(kMaximumResponseBytes + 1, 'x'), "v1.0.6"));
        CHECK(RateLimitDelayMs("", "", 1000) == 3600000);
        CHECK(RateLimitDelayMs("2", "", 1000) == 60000);
        CHECK(RateLimitDelayMs("120", "1300", 1000) == 300000);
        CHECK(RateLimitDelayMs("", "1060", 1000) == 60000);
        CHECK(RateLimitDelayMs("18446744073709551615", "", 1000) == 604800000);
        CHECK(ParseUpdateReply("").value());
        CHECK(ParseUpdateReply("  Y \r\n").value());
        CHECK(!ParseUpdateReply("n").value());
        CHECK(!ParseUpdateReply("N").value());
        CHECK(!ParseUpdateReply("yes") && !ParseUpdateReply("yn"));
        payload = nlohmann::json::parse(Body("v99.0.0"));
        const auto asset = [](const std::string& name) {
            return nlohmann::json{{"state", "uploaded"}, {"name", name},
                {"browser_download_url", "https://github.com/cs2-dma/CS2-DMA/releases/download/v99.0.0/" + name}};
        };
        payload["assets"] = nlohmann::json::array({asset("package.zip")});
        CHECK(ParseRelease(payload.dump(), "v1.0.6")->archiveName == "package.zip");
        payload["assets"].push_back(asset("other.zip"));
        CHECK(ParseRelease(payload.dump(), "v1.0.6")->archiveUrl.empty());
        payload["assets"].push_back(asset("KevqDMA_v99.0.0.zip"));
        CHECK(ParseRelease(payload.dump(), "v1.0.6")->archiveName == "KevqDMA_v99.0.0.zip");
        payload["assets"] = nlohmann::json::array({asset("package.zip")});
        payload["assets"][0]["browser_download_url"] = "https://evil.example/package.zip";
        CHECK(ParseRelease(payload.dump(), "v1.0.6")->archiveUrl.empty());
        payload["assets"][0] = asset("KevqDMA_v99.0.0.zip");
        payload["assets"][0]["state"] = "starter";
        CHECK(ParseRelease(payload.dump(), "v1.0.6")->archiveUrl.empty());
        for (const char* invalid : {"../file.zip", "a.zip?x=1", "a.zip\"", "a.zip\r\n", "src.tar.gz"})
            CHECK(!IsArchiveName(invalid));
        CHECK(!IsReleaseArchiveUrl("https://github.com/cs2-dma/CS2-DMA/releases/download/v1.0.6/a.zip", "v99.0.0"));
    }

    void TestStartup() {
        using bootstrap::StartupUpdateResult;
        app::updates::Status status;
        status.state = app::updates::State::Available;
        status.updateAvailable = true;
        status.latestTag = "v99.0.0";
        status.releaseUrl = "https://github.com/cs2-dma/CS2-DMA/releases/tag/v99.0.0";
        status.archiveUrl = "https://github.com/cs2-dma/CS2-DMA/releases/download/v99.0.0/KevqDMA_v99.0.0.zip";
        const auto newer = status;
        std::vector<std::optional<std::string>> replies;
        std::vector<std::string> urls;
        std::vector<std::string> lines;
        size_t reads = 0;
        int ok = 0, questions = 0, closes = 0;
        bool pageWorks = true, downloadWorks = true;
        bootstrap::StartupUpdateHooks hooks;
        hooks.connectionOk = [&] { ++ok; };
        hooks.connectionQuestion = [&] { ++questions; };
        hooks.info = [&](const std::string& line) { lines.push_back(line); };
        hooks.readReply = [&]() -> std::optional<std::string> {
            return reads < replies.size() ? replies[reads++] : std::nullopt;
        };
        hooks.openUrl = [&](const std::string& url) {
            urls.push_back(url);
            return urls.size() == 1 ? pageWorks : downloadWorks;
        };
        hooks.exitCountdown = [&] { ++closes; };
        const auto run = [&](std::vector<std::optional<std::string>> input) {
            replies = std::move(input); urls.clear(); lines.clear(); reads = 0;
            ok = questions = closes = 0;
            return bootstrap::HandleStartupUpdate(status, hooks);
        };
        for (const char* yes : {"", "y", "Y", "  y  "}) {
            CHECK(run({std::string(yes)}) == StartupUpdateResult::Exit);
            CHECK(questions == 1 && ok == 0 && closes == 1 && urls.size() == 2);
            CHECK(urls[0] == status.releaseUrl && urls[1] == status.archiveUrl);
        }
        CHECK(run({std::string("n")}) == StartupUpdateResult::Continue);
        CHECK(questions == 1 && ok == 1 && closes == 0 && urls.empty());
        CHECK(run({std::string("invalid"), std::string("y")}) == StartupUpdateResult::Exit);
        CHECK(reads == 2 && closes == 1);
        CHECK(run({std::nullopt}) == StartupUpdateResult::Continue);
        CHECK(ok == 1 && closes == 0 && urls.empty());
        pageWorks = false;
        CHECK(run({std::string("y")}) == StartupUpdateResult::Continue);
        CHECK(ok == 1 && closes == 0 && urls.size() == 1);
        pageWorks = true; downloadWorks = false;
        CHECK(run({std::string("y")}) == StartupUpdateResult::Exit);
        CHECK(urls.size() == 2 && closes == 1);
        downloadWorks = true;
        status.archiveUrl = "https://evil.example/other.zip";
        CHECK(run({std::string("y")}) == StartupUpdateResult::Exit);
        CHECK(urls.size() == 1 && closes == 1);
        status = newer; status.state = app::updates::State::Unavailable;
        CHECK(run({std::string("y")}) == StartupUpdateResult::Continue);
        CHECK(questions == 0 && ok == 1 && reads == 0 && urls.empty() && lines.empty());
        status = newer; status.releaseUrl = "https://evil.example/release";
        CHECK(run({std::string("y")}) == StartupUpdateResult::Continue);
        CHECK(reads == 0 && urls.empty());
        for (const std::string tag : {std::string(app::build_info::kVersionTag), std::string("v0.0.1")}) {
            status = newer; status.latestTag = tag;
            status.releaseUrl = "https://github.com/cs2-dma/CS2-DMA/releases/tag/" + tag;
            status.state = app::updates::State::Current;
            CHECK(run({std::string("y")}) == StartupUpdateResult::Continue);
            CHECK(ok == 1 && questions == 0 && reads == 0 && urls.empty());
        }
    }

    void TestFallback() {
        using namespace app::updates::policy;
        CHECK(ParseLatestRedirect("https://github.com/cs2-dma/CS2-DMA/releases/tag/v99.0.0") == "v99.0.0");
        CHECK(ParseLatestRedirect("/cs2-dma/CS2-DMA/releases/tag/v99.0.0") == "v99.0.0");
        for (const char* url : {"https://evil.example/v99.0.0", "//evil.example/v99.0.0",
                "/cs2-dma/CS2-DMA/releases/tag/v99.0.0?x=1", "/cs2-dma/CS2-DMA/releases/tag/v99.0.0-beta",
                "/cs2-dma/CS2-DMA/releases/tag/../main", "http://github.com/cs2-dma/CS2-DMA/releases/tag/v99.0.0"})
            CHECK(!ParseLatestRedirect(url));
        const std::string assets = "<a href=\"/cs2-dma/CS2-DMA/releases/download/v99.0.0/KevqDMA_v99.0.0.zip\">ZIP</a>";
        const auto links = ParseReleaseAssetLinks(assets + assets +
            "<a href=\"https://evil.example/file.zip\">x</a><a href=\"/cs2-dma/CS2-DMA/archive/v99.0.0.zip\">source</a>", "v99.0.0");
        CHECK(links.size() == 1 && links[0]["name"] == "KevqDMA_v99.0.0.zip");
        int calls = 0;
        bool failAssets = false;
        std::string location = "https://github.com/cs2-dma/CS2-DMA/releases/tag/v99.0.0";
        const ReleaseEndpointFetch fetch = [&](std::stop_token, const std::string& etag,
            const wchar_t* host, const wchar_t* path) {
            ++calls;
            CHECK(std::wstring_view(host) == L"github.com" && etag.empty());
            ReleaseResponse response;
            if (std::wstring_view(path).ends_with(L"/latest")) {
                response.status = 302; response.location = location;
            } else {
                CHECK(std::wstring_view(path) == L"/cs2-dma/CS2-DMA/releases/expanded_assets/v99.0.0");
                response.status = 200; response.body = assets;
                if (failAssets) response.error = ERROR_TIMEOUT;
            }
            return response;
        };
        auto reply = FetchReleasePage({}, fetch);
        auto release = ParseRelease(reply.body, "v1.0.6");
        CHECK(calls == 2 && reply.etag.empty() && release && release->archiveName == "KevqDMA_v99.0.0.zip");
        failAssets = true; calls = 0;
        reply = FetchReleasePage({}, fetch);
        release = ParseRelease(reply.body, "v1.0.6");
        CHECK(calls == 2 && release && release->newer && release->archiveUrl.empty());
        location = "https://github.com/cs2-dma/CS2-DMA/releases/tag/v1.0.6"; calls = 0;
        reply = FetchReleasePage({}, fetch);
        CHECK(calls == 1 && ParseRelease(reply.body, "v1.0.6")->tag == "v1.0.6");
        location = "https://evil.example/release"; calls = 0;
        reply = FetchReleasePage({}, fetch);
        CHECK(calls == 1 && reply.error == ERROR_INVALID_DATA);
        for (int blocked = 0; blocked < 2; ++blocked) {
            std::atomic<int> entered = 0;
            std::atomic<bool> cancelled = false;
            const auto before = std::chrono::steady_clock::now();
            reply = FetchReleaseWithFallback({}, "api-etag", [&](std::stop_token stop, const std::string&,
                const wchar_t* host, const wchar_t*) {
                const int index = std::wstring_view(host) == L"api.github.com" ? 0 : 1;
                ++entered;
                while (!stop.stop_requested() && (index == blocked || entered.load() < 2))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                if (stop.stop_requested()) {
                    cancelled = true;
                    return ReleaseResponse{0, ERROR_CANCELLED};
                }
                if (index == 0) return ReleaseResponse{200, 0, Body("v99.0.0"), "api-etag"};
                return ReleaseResponse{302, 0, {}, {}, {}, {},
                    "https://github.com/cs2-dma/CS2-DMA/releases/tag/v1.0.6"};
            });
            CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(750));
            CHECK(entered == 2 && cancelled && reply.error == 0 && reply.status == 200);
            CHECK(ParseRelease(reply.body, "v1.0.6")->tag == (blocked == 0 ? "v1.0.6" : "v99.0.0"));
        }
        for (const int apiStatus : {200, 304, 404, 429}) {
            reply = FetchReleaseWithFallback({}, "api-etag", [&](std::stop_token, const std::string&,
                const wchar_t* host, const wchar_t*) {
                return std::wstring_view(host) == L"api.github.com"
                    ? ReleaseResponse{static_cast<uint32_t>(apiStatus), 0, "{}", {}, "120"}
                    : ReleaseResponse{0, ERROR_WINHTTP_CANNOT_CONNECT};
            });
            CHECK(reply.status == static_cast<uint32_t>(apiStatus) && reply.error == 0);
        }
        reply = FetchReleaseWithFallback({}, {}, [](std::stop_token, const std::string&,
            const wchar_t* host, const wchar_t*) {
            return std::wstring_view(host) == L"api.github.com"
                ? ReleaseResponse{200, 0, "{}"}
                : ReleaseResponse{302, 0, {}, {}, {}, {},
                    "https://github.com/cs2-dma/CS2-DMA/releases/tag/v1.0.6"};
        });
        CHECK(ParseRelease(reply.body, "v1.0.6")->tag == "v1.0.6");
        std::stop_source cancel;
        cancel.request_stop();
        calls = 0;
        reply = FetchReleaseWithFallback(cancel.get_token(), "api-etag", fetch);
        CHECK(reply.error == ERROR_CANCELLED && calls == 0);
    }

    void TestService() {
        using app::updates::State;
        ReleaseResponse next{200, 0, Body("v99.0.0"), "\"first\"", {}, {}};
        std::atomic<int> calls = 0;
        std::string sentEtag;
        ReleaseService service([&](std::stop_token, const std::string& etag) {
            sentEtag = etag;
            ++calls;
            return next;
        });
        CHECK(!service.Request());
        const auto run = [&](int expected) {
            service.Start();
            CHECK(!service.Request());
            if (expected == 1) CHECK(service.WaitForInitialCheck(2000).state == State::Available);
            Wait([&] { return calls.load() == expected && !service.Snapshot().checking && service.Snapshot().checkedAtMs != 0; });
            service.Stop();
        };
        run(1);
        CHECK(service.Snapshot().updateAvailable && service.Snapshot().state == State::Available);
        CHECK(service.Snapshot().releaseUrl == "https://github.com/cs2-dma/CS2-DMA/releases/tag/v99.0.0");
        next = {304, 0, {}, {}, {}, {}};
        run(2);
        CHECK(sentEtag == "\"first\"" && service.Snapshot().updateAvailable);
        next = {0, ERROR_WINHTTP_TIMEOUT, {}, {}, {}, {}};
        run(3);
        CHECK(service.Snapshot().state == State::Unavailable && service.Snapshot().updateAvailable);
        next = {200, 0, Body(std::string(app::build_info::kVersionTag)), "\"current\"", {}, {}};
        run(4);
        CHECK(sentEtag.empty() && service.Snapshot().state == State::Current && !service.Snapshot().updateAvailable);
        next = {429, 0, {}, {}, "120", {}};
        run(5);
        CHECK(service.Snapshot().state == State::RateLimited && !service.Snapshot().canCheck);
        next = {404, 0, {}, {}, {}, {}};
        run(6);
        CHECK(service.Snapshot().state == State::NoRelease && service.Snapshot().latestTag.empty());
        next = {200, 0, "{}", {}, {}, {}};
        run(7);
        CHECK(service.Snapshot().state == State::InvalidRelease);
        next = {304, 0, {}, {}, {}, {}};
        run(8);
        CHECK(service.Snapshot().state == State::Unavailable);
        std::atomic<bool> entered = false;
        ReleaseService blocking([&](std::stop_token stop, const std::string&) {
            entered = true;
            while (!stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return ReleaseResponse{};
        });
        blocking.Start();
        Wait([&] { return entered.load(); });
        const auto waitStart = std::chrono::steady_clock::now();
        CHECK(blocking.WaitForInitialCheck(10).systemError == ERROR_TIMEOUT);
        CHECK(blocking.Snapshot().checking);
        CHECK(std::chrono::steady_clock::now() - waitStart < std::chrono::milliseconds(500));
        const auto before = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i) {
            CHECK(blocking.Snapshot().checking);
            CHECK(!blocking.Request());
        }
        CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(500));
        blocking.Stop();
        CHECK(!blocking.Snapshot().checking);
    }

    void TestHttp() {
        {
            HttpFixture server(Http(200, Body("v99.0.0"), "ETag: \"new\"\r\n"));
            const auto reply = FetchLatest({}, "\"old\"", L"127.0.0.1", server.port, false);
            server.worker.join();
            CHECK(reply.error == 0 && reply.status == 200 && reply.etag == "\"new\"");
            CHECK(app::updates::policy::ParseRelease(reply.body, "v1.0.6")->newer);
            CHECK(server.request.find("GET /repos/cs2-dma/CS2-DMA/releases/latest") != std::string::npos);
            CHECK(server.request.find("If-None-Match: \"old\"") != std::string::npos);
            CHECK(server.request.find("X-GitHub-Api-Version: 2022-11-28") != std::string::npos);
        }
        for (const int status : {304, 404, 429, 302}) {
            HttpFixture server(Http(status, {}, "Retry-After: 120\r\nLocation: http://127.0.0.1:1/no\r\n"));
            const auto reply = FetchLatest({}, {}, L"127.0.0.1", server.port, false);
            CHECK(reply.error == 0 && reply.status == static_cast<uint32_t>(status));
            CHECK(reply.retryAfter == "120");
        }
        {
            HttpFixture server(Http(200, std::string(app::updates::policy::kMaximumResponseBytes + 1, 'x')));
            CHECK(FetchLatest({}, {}, L"127.0.0.1", server.port, false).error == ERROR_FILE_TOO_LARGE);
        }
        for (const std::string payload : {std::string{}, std::string("HTTP/1.1 200 Test\r\nContent-Length: 999\r\nConnection: close\r\n\r\n")}) {
            HttpFixture server(payload, true);
            const auto before = std::chrono::steady_clock::now();
            const auto reply = FetchLatest({}, {}, L"127.0.0.1", server.port, false, L"/timeout", 100);
            CHECK(reply.error == ERROR_TIMEOUT);
            CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(750));
        }
        for (int attempt = 0; attempt < 12; ++attempt) {
            HttpFixture server("", true);
            std::stop_source cancellation;
            std::jthread cancel([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                cancellation.request_stop();
            });
            const auto before = std::chrono::steady_clock::now();
            const auto reply = FetchLatest(cancellation.get_token(), {}, L"127.0.0.1", server.port, false);
            CHECK(reply.error == ERROR_CANCELLED);
            CHECK(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(750));
        }
    }
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--live") {
        const auto before = std::chrono::steady_clock::now();
        const auto reply = FetchReleaseWithFallback({}, {});
        const auto release = app::updates::policy::ParseRelease(reply.body, app::build_info::kVersionTag);
        std::cout << "GitHub HTTP=" << reply.status << " OS=" << reply.error << " release=" <<
            (release ? release->tag : "unavailable") << " elapsed_ms=" <<
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count() << '\n';
        if (reply.error != 0 || reply.status != 200 || !release) return 1;
        const std::string assetPath = "/cs2-dma/CS2-DMA/releases/expanded_assets/" + release->tag;
        const std::wstring widePath(assetPath.begin(), assetPath.end());
        const auto assets = FetchLatest({}, {}, L"github.com", INTERNET_DEFAULT_HTTPS_PORT, true, widePath.c_str());
        const auto links = app::updates::policy::ParseReleaseAssetLinks(assets.body, release->tag);
        std::cout << "Assets HTTP=" << assets.status << " OS=" << assets.error << " ZIP_count=" << links.size() << '\n';
        return assets.error == 0 && assets.status == 200 && links.size() == 1 ? 0 : 1;
    }
    WSADATA data{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    TestPolicies(); TestStartup(); TestFallback(); TestService(); TestHttp();
    WSACleanup();
    if (failures) return 1;
    std::cout << "Release tests passed: numeric versions, URL validation, asynchronous lifecycle, ETag, rate limits, HTTP transport, timeout, body cap.\n";
}

#include "app/Bootstrap/version_update.h"
#include "app/Bootstrap/release_policy.h"
#include "app/Core/build_info.h"
#include "app/Platform/win_handle.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

#pragma comment(lib, "winhttp.lib")

namespace
{
    struct ReleaseResponse {
        uint32_t status = 0;
        uint32_t error = 0;
        std::string body;
        std::string etag;
        std::string retryAfter;
        std::string resetAt;
        std::string location;
    };

    std::string QueryHeader(HINTERNET request, const wchar_t* name)
    {
        std::array<wchar_t, 1024> buffer = {};
        DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name,
                buffer.data(), &size, WINHTTP_NO_HEADER_INDEX)) return {};
        std::string value;
        for (const wchar_t c : buffer) {
            if (c == 0) break;
            if (c < 32 || c > 126) return {};
            value.push_back(static_cast<char>(c));
        }
        return value;
    }

    struct AsyncResponseState {
        std::mutex mutex;
        std::condition_variable_any condition;
        DWORD completed = 0;
        DWORD error = 0;
        DWORD bytes = 0;
        std::array<char, 8192> buffer{};
        std::wstring headers;
    };

    void CALLBACK ReleaseCallback(HINTERNET, DWORD_PTR context, DWORD status, void* info, DWORD size) noexcept
    {
        if (!context) return;
        auto* binding = reinterpret_cast<std::shared_ptr<AsyncResponseState>*>(context);
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
            delete binding;
            return;
        }
        const auto state = *binding;
        std::lock_guard lock(state->mutex);
        if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && info && size >= sizeof(WINHTTP_ASYNC_RESULT))
            state->error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
        else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
                 status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE ||
                 status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
            state->completed = status;
            if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) state->bytes = size;
        }
        state->condition.notify_all();
    }

    ReleaseResponse FetchLatest(std::stop_token stop, const std::string& etag,
        const wchar_t* host = L"api.github.com", INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT,
        bool secure = true, const wchar_t* path = L"/repos/cs2-dma/CS2-DMA/releases/latest",
        uint32_t timeoutMs = 2500)
    {
        using app::platform::UniqueWinHttpHandle;
        ReleaseResponse response;
        const auto fail = [&] { response.error = GetLastError(); return response; };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::clamp(timeoutMs, 10u, 5000u));
        const std::string agent = app::build_info::HttpUserAgent();
        const std::wstring wideAgent(agent.begin(), agent.end());
        UniqueWinHttpHandle session(WinHttpOpen(wideAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
        if (!session) return fail();
        if (!WinHttpSetTimeouts(session.Get(), 1500, 1500, 1500, 2000)) return fail();
        UniqueWinHttpHandle connection(WinHttpConnect(session.Get(), host, port, 0));
        if (!connection) return fail();
        UniqueWinHttpHandle request(WinHttpOpenRequest(connection.Get(), L"GET",
            path, nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
        if (!request) return fail();
        const auto state = std::make_shared<AsyncResponseState>();
        auto binding = std::make_unique<std::shared_ptr<AsyncResponseState>>(state);
        if (WinHttpSetStatusCallback(request.Get(), ReleaseCallback,
                WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
            return fail();
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(binding.get());
        if (!WinHttpSetOption(request.Get(), WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) return fail();
        binding.release();
        DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect))) return fail();
        state->headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        if (!etag.empty() && etag.size() < 1024 && etag.find_first_of("\r\n") == std::string::npos)
            state->headers += L"If-None-Match: " + std::wstring(etag.begin(), etag.end()) + L"\r\n";
        const auto step = [&](DWORD expected, const auto& operation) {
            if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
                response.error = stop.stop_requested() ? ERROR_CANCELLED : ERROR_TIMEOUT;
                return false;
            }
            {
                std::lock_guard lock(state->mutex);
                state->completed = 0;
                state->error = 0;
            }
            if (!operation()) {
                const DWORD error = GetLastError();
                if (error != ERROR_IO_PENDING) { response.error = error; return false; }
            }
            std::unique_lock lock(state->mutex);
            const bool completed = state->condition.wait_until(lock, stop, deadline,
                [&] { return state->error != 0 || state->completed == expected; });
            if (!completed || state->error != 0) {
                response.error = stop.stop_requested() ? ERROR_CANCELLED :
                    state->error != 0 ? state->error : ERROR_TIMEOUT;
                return false;
            }
            return true;
        };
        if (!step(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, [&] {
                return WinHttpSendRequest(request.Get(), state->headers.c_str(), static_cast<DWORD>(state->headers.size()),
                    WINHTTP_NO_REQUEST_DATA, 0, 0, context);
            }) || !step(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, [&] {
                return WinHttpReceiveResponse(request.Get(), nullptr);
            })) return response;
        DWORD status = 0;
        DWORD size = sizeof(status);
        if (!WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) return fail();
        response.status = status;
        response.etag = QueryHeader(request.Get(), L"ETag");
        response.retryAfter = QueryHeader(request.Get(), L"Retry-After");
        response.resetAt = QueryHeader(request.Get(), L"X-RateLimit-Reset");
        response.location = QueryHeader(request.Get(), L"Location");
        if (status != 200) return response;
        const std::string lengthHeader = QueryHeader(request.Get(), L"Content-Length");
        const uint64_t contentLength = app::updates::policy::ParseUnsigned(lengthHeader);
        if (contentLength > app::updates::policy::kMaximumResponseBytes) {
            response.error = ERROR_FILE_TOO_LARGE;
            return response;
        }
        for (;;) {
            if (stop.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
                response.error = stop.stop_requested() ? ERROR_CANCELLED : ERROR_TIMEOUT;
                return response;
            }
            if (!step(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, [&] {
                    return WinHttpReadData(request.Get(), state->buffer.data(), static_cast<DWORD>(state->buffer.size()), nullptr);
                })) return response;
            const DWORD bytes = state->bytes;
            if (bytes == 0) {
                if (!lengthHeader.empty() && response.body.size() != contentLength)
                    response.error = ERROR_HANDLE_EOF;
                return response;
            }
            if (response.body.size() + bytes > app::updates::policy::kMaximumResponseBytes) {
                response.error = ERROR_FILE_TOO_LARGE;
                return response;
            }
            response.body.append(state->buffer.data(), bytes);
        }
    }

    using ReleaseEndpointFetch = std::function<ReleaseResponse(std::stop_token, const std::string&,
        const wchar_t*, const wchar_t*)>;

    ReleaseResponse FetchReleasePage(std::stop_token stop, const ReleaseEndpointFetch& fetch)
    {
        using namespace app::updates::policy;
        auto page = fetch(stop, {}, L"github.com", L"/cs2-dma/CS2-DMA/releases/latest");
        if (stop.stop_requested() || page.error != 0 ||
            (page.status != 301 && page.status != 302 && page.status != 307 && page.status != 308)) return page;
        const auto tag = ParseLatestRedirect(page.location);
        if (!tag) { page.error = ERROR_INVALID_DATA; return page; }
        nlohmann::json release{{"tag_name", *tag}, {"html_url", std::string(kReleasesUrl) + "/tag/" + *tag},
            {"draft", false}, {"prerelease", false}};
        if (*ParseVersion(*tag) > *ParseVersion(app::build_info::kVersionTag)) {
            const std::string path = "/cs2-dma/CS2-DMA/releases/expanded_assets/" + *tag;
            const std::wstring widePath(path.begin(), path.end());
            const auto assets = fetch(stop, {}, L"github.com", widePath.c_str());
            if (assets.error == 0 && assets.status == 200)
                release["assets"] = ParseReleaseAssetLinks(assets.body, *tag);
        }
        ReleaseResponse result;
        result.status = 200;
        result.body = release.dump();
        return result;
    }

    ReleaseResponse FetchReleaseWithFallback(std::stop_token stop, const std::string& etag,
        const ReleaseEndpointFetch& fetch = [](std::stop_token token, const std::string& conditional,
            const wchar_t* host, const wchar_t* path) {
            return FetchLatest(token, conditional, host, INTERNET_DEFAULT_HTTPS_PORT, true, path,
                std::wstring_view(path).find(L"/expanded_assets/") != std::wstring_view::npos ? 800u : 2500u);
        })
    {
        if (stop.stop_requested()) { ReleaseResponse result; result.error = ERROR_CANCELLED; return result; }
        std::mutex mutex;
        std::condition_variable_any condition;
        std::array<ReleaseResponse, 2> responses;
        int completed = 0;
        int winner = -1;
        std::stop_source cancellation;
        std::stop_callback parentStop(stop, [&] { cancellation.request_stop(); condition.notify_all(); });
        const auto run = [&](int index) {
            ReleaseResponse response;
            bool valid = false;
            try {
                response = index == 0
                    ? fetch(cancellation.get_token(), etag, L"api.github.com", L"/repos/cs2-dma/CS2-DMA/releases/latest")
                    : FetchReleasePage(cancellation.get_token(), fetch);
                valid = response.error == 0 &&
                    ((response.status == 304 && index == 0 && !etag.empty()) ||
                     (response.status == 200 && app::updates::policy::ParseRelease(response.body, app::build_info::kVersionTag)));
            } catch (...) { response.error = ERROR_INVALID_DATA; }
            std::lock_guard lock(mutex);
            responses[index] = std::move(response);
            if (valid && winner < 0) winner = index;
            ++completed;
            condition.notify_all();
        };
        std::jthread api;
        std::jthread page;
        try {
            api = std::jthread([&] { run(0); });
            page = std::jthread([&] { run(1); });
        } catch (...) {
            cancellation.request_stop();
            if (api.joinable()) api.join();
            ReleaseResponse result;
            result.error = ERROR_NOT_ENOUGH_MEMORY;
            return result;
        }
        bool finished = false;
        {
            std::unique_lock lock(mutex);
            finished = condition.wait_for(lock, stop, std::chrono::milliseconds(3000),
                [&] { return winner >= 0 || completed == 2; });
        }
        cancellation.request_stop();
        api.join();
        page.join();
        if (stop.stop_requested() || !finished) {
            ReleaseResponse result;
            result.error = stop.stop_requested() ? ERROR_CANCELLED : ERROR_TIMEOUT;
            return result;
        }
        return std::move(responses[winner >= 0 ? winner : 0]);
    }

    class ReleaseService
    {
    public:
        using Fetch = std::function<ReleaseResponse(std::stop_token, const std::string&)>;

        explicit ReleaseService(Fetch fetch = [](std::stop_token stop, const std::string& etag) {
            return FetchReleaseWithFallback(stop, etag);
        }) : fetch_(std::move(fetch)) {}

        ~ReleaseService() { Stop(); }

        void Start()
        {
            std::lock_guard lifecycle(lifecycle_);
            if (worker_.joinable()) return;
            {
                std::lock_guard lock(mutex_);
                stopping_ = false;
                requested_ = true;
            }
            try {
                worker_ = std::jthread([this](std::stop_token stop) { Run(stop); });
            } catch (...) {
                std::lock_guard lock(mutex_);
                stopping_ = true;
                requested_ = false;
                status_.state = app::updates::State::Unavailable;
                status_.systemError = ERROR_NOT_ENOUGH_MEMORY;
                condition_.notify_all();
            }
        }

        void Stop() noexcept
        {
            std::lock_guard lifecycle(lifecycle_);
            if (!worker_.joinable()) return;
            {
                std::lock_guard lock(mutex_);
                stopping_ = true;
            }
            worker_.request_stop();
            condition_.notify_all();
            worker_.join();
        }

        app::updates::Status Snapshot() const
        {
            std::lock_guard lock(mutex_);
            auto result = status_;
            result.canCheck = !stopping_ && !requested_ && !status_.checking && GetTickCount64() >= manualAtMs_;
            return result;
        }

        bool Request()
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || requested_ || status_.checking || GetTickCount64() < manualAtMs_) return false;
            requested_ = true;
            condition_.notify_one();
            return true;
        }

        app::updates::Status WaitForInitialCheck(uint32_t timeoutMs)
        {
            std::unique_lock lock(mutex_);
            const bool completed = condition_.wait_for(lock, std::chrono::milliseconds(std::min(timeoutMs, 10000u)),
                [&] { return status_.checkedAtMs != 0 || stopping_; });
            auto result = status_;
            if (!completed) {
                result.checking = false;
                result.state = app::updates::State::Unavailable;
                result.systemError = ERROR_TIMEOUT;
            }
            return result;
        }

    private:
        void Run(std::stop_token stop) noexcept
        {
            try {
                std::unique_lock lock(mutex_);
                while (!stop.stop_requested()) {
                    if (!requested_) {
                        const uint64_t now = GetTickCount64();
                        if (now < automaticAtMs_) {
                            condition_.wait_for(lock, std::chrono::milliseconds(automaticAtMs_ - now),
                                [&] { return requested_ || stop.stop_requested(); });
                            continue;
                        }
                    }
                    if (stop.stop_requested()) break;
                    requested_ = false;
                    status_.checking = true;
                    const std::string etag = etag_;
                    const uint64_t started = GetTickCount64();
                    lock.unlock();
                    ReleaseResponse response;
                    std::optional<app::updates::policy::Release> release;
                    try {
                        response = fetch_(stop, etag);
                        if (response.error == 0 && response.status == 200)
                            release = app::updates::policy::ParseRelease(response.body, app::build_info::kVersionTag);
                    } catch (...) { response.error = ERROR_INVALID_DATA; }
                    lock.lock();
                    status_.checking = false;
                    if (stop.stop_requested()) break;
                    const uint64_t now = GetTickCount64();
                    status_.checkedAtMs = now;
                    status_.elapsedMs = now - started;
                    status_.httpStatus = response.status;
                    status_.systemError = response.error;
                    manualAtMs_ = now + app::updates::policy::kManualCooldownMs;
                    automaticAtMs_ = now + app::updates::policy::kAutomaticIntervalMs;
                    using State = app::updates::State;
                    if (response.error != 0) status_.state = State::Unavailable;
                    else if (response.status == 304 && !etag_.empty() && !status_.latestTag.empty())
                        status_.state = status_.updateAvailable ? State::Available : State::Current;
                    else if (response.status == 200 && release) {
                        status_.latestTag = release->tag;
                        status_.releaseUrl = release->url;
                        status_.archiveName = release->archiveName;
                        status_.archiveUrl = release->archiveUrl;
                        status_.updateAvailable = release->newer;
                        status_.state = release->newer ? State::Available : State::Current;
                        etag_ = response.etag;
                    } else if (response.status == 404) {
                        status_.state = State::NoRelease;
                        status_.latestTag.clear();
                        status_.releaseUrl.clear();
                        status_.archiveName.clear();
                        status_.archiveUrl.clear();
                        status_.updateAvailable = false;
                        etag_.clear();
                    } else if (response.status == 403 || response.status == 429) {
                        status_.state = State::RateLimited;
                        const auto unixNow = static_cast<uint64_t>(std::time(nullptr));
                        manualAtMs_ = now + app::updates::policy::RateLimitDelayMs(response.retryAfter, response.resetAt, unixNow);
                        automaticAtMs_ = manualAtMs_;
                    } else status_.state = response.status == 200 ? State::InvalidRelease : State::Unavailable;
                    if (response.status != 304 && !release && response.status != 403 && response.status != 429)
                        etag_.clear();
                    condition_.notify_all();
                }
            } catch (...) {
                std::lock_guard lock(mutex_);
                status_.checking = false;
                status_.state = app::updates::State::Unavailable;
                status_.systemError = ERROR_NOT_ENOUGH_MEMORY;
                stopping_ = true;
                condition_.notify_all();
            }
        }

        Fetch fetch_;
        mutable std::mutex mutex_;
        std::mutex lifecycle_;
        std::condition_variable condition_;
        app::updates::Status status_;
        std::string etag_;
        bool stopping_ = true;
        bool requested_ = false;
        uint64_t manualAtMs_ = 0;
        uint64_t automaticAtMs_ = 0;
        std::jthread worker_;
    };

    ReleaseService& Service() { static ReleaseService service; return service; }
}

void app::updates::Start() noexcept { try { Service().Start(); } catch (...) {} }
void app::updates::Shutdown() noexcept { try { Service().Stop(); } catch (...) {} }
bool app::updates::RequestCheck() noexcept { try { return Service().Request(); } catch (...) { return false; } }
app::updates::Status app::updates::GetStatus() { return Service().Snapshot(); }
app::updates::Status app::updates::WaitForInitialCheck(uint32_t timeoutMs) { return Service().WaitForInitialCheck(timeoutMs); }

#pragma once
// SDK 的统一结果类型：值存在即成功；失败时保留分类、端点、字段路径与服务端业务码。
// 调用方不需要依赖 QVariantMap 或异常来判断成功与否。
#include <QString>
#include <optional>
#include <utility>

namespace ncm::sdk {

enum class ErrorKind {
    InvalidRequest,   // 本地参数不合法，没有发请求
    Network,          // 没拿到 HTTP 响应
    Api,              // 服务端回了业务码（code != 200）
    Parse,            // 响应结构不符合 SDK 契约
};

struct Error {
    ErrorKind kind = ErrorKind::Parse;
    QString endpoint;   // 请求路径，例如 api/link/page/...
    QString path;       // 失败字段的 JSON path，例如 wiki.data.blocks[0].rnData
    int code = 0;       // 网易业务码；0 表示没有
    int httpStatus = 0; // HTTP 状态；0 表示没有响应
    QString message;    // 给日志/开发者看的原文
};

template <typename T>
struct Result {
    std::optional<T> value;
    Error error;

    static Result success(T v)
    {
        Result r;
        r.value = std::move(v);
        return r;
    }
    static Result failure(Error e)
    {
        Result r;
        r.error = std::move(e);
        return r;
    }
    bool ok() const { return value.has_value(); }
    explicit operator bool() const { return ok(); }
};

} // namespace ncm::sdk

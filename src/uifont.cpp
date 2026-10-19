#include "uifont.h"

#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QLocale>

#include <fontconfig/fontconfig.h>

namespace UiFont {
namespace {

bool traditional()
{
    return QLocale::system().script() == QLocale::TraditionalHanScript;
}

// 看字形而不看语言标记：鸿蒙黑体有假名，但 fontconfig 没把它标成日文
bool coversCjk(FcPattern *font)
{
    FcCharSet *charset = nullptr;
    return FcPatternGetCharSet(font, FC_CHARSET, 0, &charset) == FcResultMatch
        && FcCharSetHasChar(charset, 0x4E00)    // 一
        && FcCharSetHasChar(charset, 0x30AB);   // カ
}

QString familyOf(FcPattern *font)
{
    FcChar8 *name = nullptr;
    return FcPatternGetString(font, FC_FAMILY, 0, &name) == FcResultMatch
        ? QString::fromUtf8(reinterpret_cast<const char *>(name)) : QString();
}

// 装着的字体里，family 本身或者以「family 」开头的同族款中第一款有中日文字形的；
// 同族款里名字带 SC / TC（按界面语言）的优先。exact 时只看 family 本身
QString installed(const QString &family, bool exact)
{
    FcPattern *pattern = FcPatternCreate();
    FcObjectSet *objects = FcObjectSetBuild(FC_FAMILY, FC_CHARSET, nullptr);
    FcFontSet *fonts = FcFontList(nullptr, pattern, objects);
    const QString marker = traditional() ? QStringLiteral(" TC") : QStringLiteral(" SC");
    QString found;
    for (int i = 0; fonts && i < fonts->nfont; ++i) {
        const QString name = familyOf(fonts->fonts[i]);
        const bool match = exact ? name == family : name.startsWith(family + u' ');
        if (!match || !coversCjk(fonts->fonts[i]))
            continue;
        if (exact || name.contains(marker)) {
            found = name;
            break;
        }
        if (found.isEmpty())
            found = name;
    }
    if (fonts)
        FcFontSetDestroy(fonts);
    FcObjectSetDestroy(objects);
    FcPatternDestroy(pattern);
    return found;
}

// fontconfig 为 family 在界面语言下排出的回退顺序里，第一款有中日文字形的
QString fontconfigFallback(const QString &family)
{
    FcPattern *pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8 *>(family.toUtf8().constData()));
    FcPatternAddString(pattern, FC_LANG, reinterpret_cast<const FcChar8 *>(traditional() ? "zh-tw" : "zh-cn"));
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result = FcResultNoMatch;
    FcFontSet *fonts = FcFontSort(nullptr, pattern, FcFalse, nullptr, &result);
    QString found;
    for (int i = 0; fonts && i < fonts->nfont && found.isEmpty(); ++i) {
        if (coversCjk(fonts->fonts[i]))
            found = familyOf(fonts->fonts[i]);
    }
    if (fonts)
        FcFontSetDestroy(fonts);
    FcPatternDestroy(pattern);
    return found;
}

void apply()
{
    QFont font = QGuiApplication::font();
    if (font.families().size() > 1)
        return;   // 已经排过（或别处已经给了回退列表）
    const QString family = font.family();
    if (!installed(family, true).isEmpty())
        return;   // 系统字体本身就是中文字体：汉字、假名都在它上面
    static const bool sandboxed = QFile::exists(QStringLiteral("/.flatpak-info"));
    QString cjk = sandboxed ? installed(family, false) : QString();
    if (cjk.isEmpty())
        cjk = fontconfigFallback(family);
    if (cjk.isEmpty())
        return;   // 系统里没有中日文字体
    font.setFamilies({family, cjk});
    QGuiApplication::setFont(font);
}

} // namespace

void unifyCjk()
{
    apply();
    QObject::connect(qGuiApp, &QGuiApplication::fontChanged, qGuiApp, [] { apply(); });
}

} // namespace UiFont

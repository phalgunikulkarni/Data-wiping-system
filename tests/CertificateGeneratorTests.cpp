#include "../src/CertificateGenerator.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <cassert>

int main()
{
    CertificateData data{};
    data.certificateID = "id<&\"";
    data.organizationName = "München & <Partners> \"Ops\"";
    data.deviceModel = "Disk <script>alert(1)</script>";
    data.status = "success\" onmouseover=\"x";
    data.comments = "R&D 'quoted'";
    data.success = true;
    data.verificationPassed = true;
    data.passes = 1;
    data.bytesWiped = 4096;
    data.postwipeHash = std::string(64, 'a');

    const std::string html = CertificateGenerator::generateHTML(data);
    assert(html.find("id&lt;&amp;&quot;") != std::string::npos);
    assert(html.find("Disk &lt;script&gt;alert(1)&lt;/script&gt;") != std::string::npos);
    assert(html.find("München &amp; &lt;Partners&gt; &quot;Ops&quot;") != std::string::npos);
    assert(html.find("permanently unrecoverable") == std::string::npos);
    assert(html.find("Unverified or failed") != std::string::npos);

    const std::string json = CertificateGenerator::generateJSON(data);
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
    assert(parseError.error == QJsonParseError::NoError && document.isObject());
    const QJsonObject root = document.object();
    assert(root.value(QStringLiteral("certificate")).toObject().value(QStringLiteral("success")).toBool() == false);
    assert(root.value(QStringLiteral("organization")).toObject().value(QStringLiteral("name")).toString() == QString::fromUtf8(data.organizationName));
    assert(root.value(QStringLiteral("limitations")).toString().contains(QStringLiteral("not independently authenticated")));
    return 0;
}

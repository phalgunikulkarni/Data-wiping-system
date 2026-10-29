#include "CertificateGenerator.h"
#include <ctime>
#include <sstream>
#include <fstream>
#include <random>
#include <iomanip>
#include <windows.h>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>

namespace {
QString qstr(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
std::string htmlEscape(const std::string& value) { return qstr(value).toHtmlEscaped().toUtf8().toStdString(); }
bool evidenceSupportsSuccess(const CertificateData& data)
{
    // CertificateData is supplied by callers and is not linked to authenticated,
    // persisted WipeResult/VerificationResult evidence. Never promote it to proof.
    (void)data;
    return false;
}
}

std::string CertificateGenerator::generateCertificateID() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    
    char dateStr[9];
    strftime(dateStr, sizeof(dateStr), "%Y%m%d", &timeinfo);
    
    // Format: WE-20260615-XXXXXXXXXXXX
    std::stringstream ss;
    ss << "CERT-" << dateStr << "-";
    
    for (int i = 0; i < 8; i++) {
        ss << std::hex << dis(gen);
    }
    
    return ss.str();
}

std::string CertificateGenerator::getHTMLHeader(const CertificateData& data) {
    std::stringstream ss;
    ss << "<!DOCTYPE html>\n";
    ss << "<html lang=\"en\">\n";
    ss << "<head>\n";
    ss << "    <meta charset=\"UTF-8\">\n";
    ss << "    <title>DataGuard Operation Record - " << htmlEscape(data.certificateID) << "</title>\n";
    ss << "    <style>\n";
    ss << "        * { margin: 0; padding: 0; box-sizing: border-box; }\n";
    ss << "        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background: #f5f5f5; }\n";
    ss << "        .container { max-width: 900px; margin: 40px auto; background: white; padding: 40px; box-shadow: 0 0 20px rgba(0,0,0,0.1); border-top: 5px solid #2ecc71; }\n";
    ss << "        .header { text-align: center; border-bottom: 2px solid #34495e; padding-bottom: 20px; margin-bottom: 30px; }\n";
    ss << "        .header h1 { color: #2c3e50; font-size: 32px; margin-bottom: 5px; }\n";
    ss << "        .header p { color: #7f8c8d; font-size: 12px; letter-spacing: 2px; }\n";
    ss << "        .certificate-id { background: #ecf0f1; padding: 15px; border-radius: 5px; margin: 20px 0; text-align: center; }\n";
    ss << "        .certificate-id strong { font-size: 14px; color: #2c3e50; }\n";
    ss << "        .section { margin-bottom: 30px; }\n";
    ss << "        .section h2 { color: #34495e; font-size: 16px; border-bottom: 2px solid #3498db; padding-bottom: 10px; margin-bottom: 15px; text-transform: uppercase; letter-spacing: 1px; }\n";
    ss << "        .row { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; margin-bottom: 10px; }\n";
    ss << "        .row.full { grid-template-columns: 1fr; }\n";
    ss << "        .field { }\n";
    ss << "        .field label { display: block; font-weight: 600; color: #2c3e50; font-size: 12px; margin-bottom: 5px; text-transform: uppercase; letter-spacing: 0.5px; }\n";
    ss << "        .field value { display: block; background: #f8f9fa; padding: 10px; border-left: 3px solid #3498db; color: #2c3e50; font-family: 'Courier New', monospace; font-size: 13px; }\n";
    ss << "        .status-success { color: #27ae60; font-weight: bold; font-size: 14px; }\n";
    ss << "        .status-failed { color: #e74c3c; font-weight: bold; font-size: 14px; }\n";
    ss << "        .verification { background: #f0f8ff; padding: 15px; border-radius: 5px; border-left: 4px solid #3498db; }\n";
    ss << "        .verification h3 { color: #2c3e50; font-size: 13px; margin-bottom: 10px; }\n";
    ss << "        .hash-display { font-family: monospace; font-size: 11px; word-break: break-all; background: white; padding: 10px; border-radius: 3px; margin: 5px 0; }\n";
    ss << "        .footer { border-top: 2px solid #ecf0f1; padding-top: 20px; margin-top: 30px; text-align: center; color: #7f8c8d; font-size: 11px; }\n";
    ss << "        .seal { text-align: center; margin: 20px 0; font-size: 48px; opacity: 0.3; }\n";
    ss << "        .standards { background: #fff8dc; padding: 15px; border-radius: 5px; font-size: 12px; color: #654321; }\n";
    ss << "        .standards strong { display: block; margin-bottom: 8px; color: #2c3e50; }\n";
    ss << "        table { width: 100%; border-collapse: collapse; margin-top: 10px; }\n";
    ss << "        th, td { padding: 10px; text-align: left; border-bottom: 1px solid #ecf0f1; }\n";
    ss << "        th { background: #34495e; color: white; font-weight: 600; }\n";
    ss << "        tr:hover { background: #f8f9fa; }\n";
    ss << "        .warning { background: #ffe4e4; padding: 12px; border-left: 4px solid #e74c3c; color: #c0392b; font-size: 12px; margin: 10px 0; border-radius: 3px; }\n";
    ss << "        @media print { body { background: white; } .container { box-shadow: none; border: none; } }\n";
    ss << "    </style>\n";
    ss << "</head>\n";
    ss << "<body>\n";
    ss << "<div class=\"container\">\n";
    return ss.str();
}

std::string CertificateGenerator::getHTMLBody(const CertificateData& data) {
    std::stringstream ss;
    const bool evidenceSuccess = evidenceSupportsSuccess(data);
    
    // Header
    ss << "<div class=\"header\">\n";
    ss << "    <h1>DataGuard Operation Record</h1>\n";
    ss << "    <p>Supplied operation details; not an independent certification</p>\n";
    ss << "</div>\n";
    
    ss << "<div class=\"certificate-id\">\n";
    ss << "    <strong>Certificate ID: " << htmlEscape(data.certificateID) << "</strong>\n";
    ss << "</div>\n";
    
    // Status
    ss << "<div class=\"section\">\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Status</label>\n";
    ss << "            <value class=\"" << (evidenceSuccess ? "status-success" : "status-failed") << "\">\n";
    ss << "            " << (evidenceSuccess ? "Reported success with read-back evidence fields" : "Unverified or failed") << "\n";
    ss << "            </value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Issue Date</label>\n";
    ss << "            <value>" << htmlEscape(data.issueDate) << " " << htmlEscape(data.issueTime) << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "</div>\n";
    
    // Device Information
    ss << "<div class=\"section\">\n";
    ss << "    <h2>Device Information</h2>\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Device Model</label>\n";
    ss << "            <value>" << htmlEscape(data.deviceModel) << "</value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Serial Number</label>\n";
    ss << "            <value>" << htmlEscape(data.serialNumber) << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Device Path</label>\n";
    ss << "            <value>" << htmlEscape(data.devicePath) << "</value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>File System</label>\n";
    ss << "            <value>" << htmlEscape(data.fileSystem) << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Capacity</label>\n";
    ss << "            <value>" << data.capacityGB << " GB</value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Bytes Wiped</label>\n";
    ss << "            <value>" << data.bytesWiped << " bytes</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "</div>\n";
    
    // Wipe Method
    ss << "<div class=\"section\">\n";
    ss << "    <h2>Wipe Method</h2>\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Algorithm</label>\n";
    ss << "            <value>" << htmlEscape(data.wipeMethod) << "</value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Standard</label>\n";
    ss << "            <value>" << htmlEscape(data.wipeStandard) << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Number of Passes</label>\n";
    ss << "            <value>" << data.passes << " pass(es)</value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Completion</label>\n";
    ss << "            <value>" << htmlEscape(data.completionDate) << " " << htmlEscape(data.completionTime) << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "</div>\n";
    
    // Verification
    ss << "<div class=\"section\">\n";
    ss << "    <h2>Verification & Integrity</h2>\n";
    ss << "    <div class=\"verification\">\n";
    ss << "        <h3>Pre-Wipe Hash (SHA-256)</h3>\n";
    ss << "        <div class=\"hash-display\">" << htmlEscape(data.prewipeHash) << "</div>\n";
    ss << "        <h3>Post-Wipe Hash (SHA-256)</h3>\n";
    ss << "        <div class=\"hash-display\">" << htmlEscape(data.postwipeHash) << "</div>\n";
    ss << "        <h3 style=\"margin-top: 15px;\">Verification Result</h3>\n";
    ss << "        <value style=\"margin-top: 10px;\" class=\"" << (evidenceSuccess ? "status-success" : "status-failed") << "\">\n";
    ss << "        " << (evidenceSuccess ? "READ-BACK EVIDENCE REPORTED" : "NOT VERIFIED") << "\n";
    ss << "        </value>\n";
    ss << "        <p style=\"margin-top: 10px; color: #7f8c8d; font-size: 12px;\">" << htmlEscape(data.verificationNotes) << "</p>\n";
    ss << "    </div>\n";
    ss << "</div>\n";
    
    // Standards
    ss << "<div class=\"section\">\n";
    ss << "    <h2>Compliance</h2>\n";
    ss << "    <div class=\"standards\">\n";
    ss << "        <strong>Certification:</strong>\n";
    ss << "        No independent certification or standards compliance is asserted.<br>\n";
    ss << "        <br>\n";
    ss << "        <strong>Limitations:</strong><br>\n";
    ss << "        This record reflects supplied fields only. It does not prove physical erasure, cover remapped or overprovisioned storage, or guarantee that data is unrecoverable.\n";
    ss << "    </div>\n";
    ss << "</div>\n";
    
    // Organization
    ss << "<div class=\"section\">\n";
    ss << "    <h2>Organization</h2>\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Organization</label>\n";
    ss << "            <value>" << htmlEscape(data.organizationName) << "</value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Department</label>\n";
    ss << "            <value>" << htmlEscape(data.department) << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "    <div class=\"row full\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Technician / Operator</label>\n";
    ss << "            <value>" << htmlEscape(data.technician) << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "</div>\n";
    
    // Comments
    if (!data.comments.empty()) {
        ss << "<div class=\"section\">\n";
        ss << "    <h2>Additional Notes</h2>\n";
        ss << "    <div class=\"field\">\n";
        ss << "        <value>" << htmlEscape(data.comments) << "</value>\n";
        ss << "    </div>\n";
        ss << "</div>\n";
    }
    
    ss << "<div class=\"footer\">This operation record is not digitally signed.</div>\n";
    
    return ss.str();
}

std::string CertificateGenerator::getHTMLFooter() {
    return "</div>\n</body>\n</html>\n";
}

std::string CertificateGenerator::generateHTML(const CertificateData& data) {
    return getHTMLHeader(data) + getHTMLBody(data) + getHTMLFooter();
}

std::string CertificateGenerator::generateText(const CertificateData& data) {
    std::stringstream ss;
    const bool evidenceSuccess = evidenceSupportsSuccess(data);
    ss << "╔════════════════════════════════════════════════════════════════════════════════╗\n";
    ss << "║                    DataGuard Operation Record                                 ║\n";
    ss << "║              Supplied details; not an independent certification               ║\n";
    ss << "╠════════════════════════════════════════════════════════════════════════════════╣\n";
    ss << "║ Certificate ID: " << std::left << std::setw(68) << data.certificateID << "║\n";
    ss << "║ Status: " << std::setw(77) << (evidenceSuccess ? "REPORTED SUCCESS; EVIDENCE FIELDS PRESENT" : "UNVERIFIED OR FAILED") << "║\n";
    ss << "╠════════════════════════════════════════════════════════════════════════════════╣\n";
    ss << "║ DEVICE INFORMATION\n";
    ss << "║ Model: " << data.deviceModel << "\n";
    ss << "║ Serial: " << data.serialNumber << "\n";
    ss << "║ Capacity: " << data.capacityGB << " GB\n";
    ss << "║ File System: " << data.fileSystem << "\n";
    ss << "╠════════════════════════════════════════════════════════════════════════════════╣\n";
    ss << "║ WIPE METHOD\n";
    ss << "║ Algorithm: " << data.wipeMethod << "\n";
    ss << "║ Standard: " << data.wipeStandard << "\n";
    ss << "║ Passes: " << data.passes << "\n";
    ss << "║ Bytes Wiped: " << data.bytesWiped << "\n";
    ss << "╠════════════════════════════════════════════════════════════════════════════════╣\n";
    ss << "║ VERIFICATION\n";
    ss << "║ Verification: " << (evidenceSuccess ? "READ-BACK EVIDENCE REPORTED" : "NOT VERIFIED") << "\n";
    ss << "║ No physical erasure, standards compliance, or permanent unrecoverability is guaranteed.\n";
    ss << "╚════════════════════════════════════════════════════════════════════════════════╝\n";
    return ss.str();
}

std::string CertificateGenerator::generateJSON(const CertificateData& data) {
    const bool evidenceSuccess = evidenceSupportsSuccess(data);
    QJsonObject certificate;
    certificate.insert(QStringLiteral("id"), qstr(data.certificateID));
    certificate.insert(QStringLiteral("version"), QStringLiteral("1.0.0"));
    certificate.insert(QStringLiteral("issued"), qstr(data.issueDate) + QLatin1Char('T') + qstr(data.issueTime));
    certificate.insert(QStringLiteral("completed"), qstr(data.completionDate) + QLatin1Char('T') + qstr(data.completionTime));
    certificate.insert(QStringLiteral("reportedStatus"), qstr(data.status));
    certificate.insert(QStringLiteral("success"), evidenceSuccess);
    certificate.insert(QStringLiteral("verificationEvidencePresent"), evidenceSuccess);

    QJsonObject device;
    device.insert(QStringLiteral("model"), qstr(data.deviceModel));
    device.insert(QStringLiteral("serialNumber"), qstr(data.serialNumber));
    device.insert(QStringLiteral("path"), qstr(data.devicePath));
    device.insert(QStringLiteral("capacityGB"), QString::number(data.capacityGB));
    device.insert(QStringLiteral("fileSystem"), qstr(data.fileSystem));

    QJsonObject wipe;
    wipe.insert(QStringLiteral("algorithm"), qstr(data.wipeMethod));
    wipe.insert(QStringLiteral("reportedStandard"), qstr(data.wipeStandard));
    wipe.insert(QStringLiteral("passes"), data.passes);
    wipe.insert(QStringLiteral("bytesWiped"), QString::number(data.bytesWiped));

    QJsonObject verification;
    verification.insert(QStringLiteral("prewipeHash"), qstr(data.prewipeHash));
    verification.insert(QStringLiteral("postwipeHash"), qstr(data.postwipeHash));
    verification.insert(QStringLiteral("reportedPassed"), data.verificationPassed);
    verification.insert(QStringLiteral("notes"), qstr(data.verificationNotes));

    QJsonObject organization;
    organization.insert(QStringLiteral("name"), qstr(data.organizationName));
    organization.insert(QStringLiteral("department"), qstr(data.department));
    organization.insert(QStringLiteral("technician"), qstr(data.technician));
    organization.insert(QStringLiteral("comments"), qstr(data.comments));

    QJsonObject root;
    root.insert(QStringLiteral("recordType"), QStringLiteral("unverified-operation-record"));
    root.insert(QStringLiteral("certificate"), certificate);
    root.insert(QStringLiteral("device"), device);
    root.insert(QStringLiteral("wipeMethod"), wipe);
    root.insert(QStringLiteral("verification"), verification);
    root.insert(QStringLiteral("organization"), organization);
    root.insert(QStringLiteral("limitations"), QStringLiteral("Supplied fields are not independently authenticated. No claim of physical erasure, standards certification, or permanent unrecoverability is made."));
    return QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString();
}
bool CertificateGenerator::saveCertificate(const CertificateData& data, 
                                          const std::string& format) {
    std::string filename;
    std::string content;
    std::string ext;
    
    if (format == "html") {
        ext = ".html";
        content = generateHTML(data);
    } else if (format == "text") {
        ext = ".txt";
        content = generateText(data);
    } else if (format == "json") {
        ext = ".json";
        content = generateJSON(data);
    } else {
        return false;
    }
    
    if (data.certificateID.empty() || data.certificateID.size() > 80 ||
        !std::all_of(data.certificateID.begin(), data.certificateID.end(), [](unsigned char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                   (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
        })) return false;
    filename = "Certificate_" + data.certificateID + ext;
    
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    file << content;
    file.close();
    
    return true;
}

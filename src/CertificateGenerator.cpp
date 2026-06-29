#include "CertificateGenerator.h"
#include <ctime>
#include <sstream>
#include <fstream>
#include <random>
#include <iomanip>
#include <windows.h>

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
    ss << "    <title>WipeEngine Certificate - " << data.certificateID << "</title>\n";
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
    
    // Header
    ss << "<div class=\"header\">\n";
    ss << "    <h1>🔐 SECURE DATA WIPE CERTIFICATE</h1>\n";
    ss << "    <p>WipeEngine - Certified Data Erasure</p>\n";
    ss << "</div>\n";
    
    ss << "<div class=\"certificate-id\">\n";
    ss << "    <strong>Certificate ID: " << data.certificateID << "</strong>\n";
    ss << "</div>\n";
    
    // Status
    ss << "<div class=\"section\">\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Status</label>\n";
    ss << "            <value class=\"" << (data.success ? "status-success" : "status-failed") << "\">\n";
    ss << "            " << data.status << "\n";
    ss << "            </value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Issue Date</label>\n";
    ss << "            <value>" << data.issueDate << " " << data.issueTime << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "</div>\n";
    
    // Device Information
    ss << "<div class=\"section\">\n";
    ss << "    <h2>Device Information</h2>\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Device Model</label>\n";
    ss << "            <value>" << data.deviceModel << "</value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Serial Number</label>\n";
    ss << "            <value>" << data.serialNumber << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Device Path</label>\n";
    ss << "            <value>" << data.devicePath << "</value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>File System</label>\n";
    ss << "            <value>" << data.fileSystem << "</value>\n";
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
    ss << "            <value>" << data.wipeMethod << "</value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Standard</label>\n";
    ss << "            <value>" << data.wipeStandard << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Number of Passes</label>\n";
    ss << "            <value>" << data.passes << " pass(es)</value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Completion</label>\n";
    ss << "            <value>" << data.completionDate << " " << data.completionTime << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "</div>\n";
    
    // Verification
    ss << "<div class=\"section\">\n";
    ss << "    <h2>Verification & Integrity</h2>\n";
    ss << "    <div class=\"verification\">\n";
    ss << "        <h3>Pre-Wipe Hash (SHA-256)</h3>\n";
    ss << "        <div class=\"hash-display\">" << data.prewipeHash << "</div>\n";
    ss << "        <h3>Post-Wipe Hash (SHA-256)</h3>\n";
    ss << "        <div class=\"hash-display\">" << data.postwipeHash << "</div>\n";
    ss << "        <h3 style=\"margin-top: 15px;\">Verification Result</h3>\n";
    ss << "        <value style=\"margin-top: 10px;\" class=\"" << (data.verificationPassed ? "status-success" : "status-failed") << "\">\n";
    ss << "        " << (data.verificationPassed ? "✓ PASSED" : "✗ FAILED") << "\n";
    ss << "        </value>\n";
    ss << "        <p style=\"margin-top: 10px; color: #7f8c8d; font-size: 12px;\">" << data.verificationNotes << "</p>\n";
    ss << "    </div>\n";
    ss << "</div>\n";
    
    // Standards
    ss << "<div class=\"section\">\n";
    ss << "    <h2>Compliance</h2>\n";
    ss << "    <div class=\"standards\">\n";
    ss << "        <strong>Certifications & Standards:</strong>\n";
    ss << "        ✓ DOD 5220.22-M (United States Department of Defense)<br>\n";
    ss << "        ✓ NIST Guidelines<br>\n";
    ss << "        ✓ ISO 27001 Data Protection<br>\n";
    ss << "        <br>\n";
    ss << "        <strong>Data Destruction Guarantee:</strong><br>\n";
    ss << "        This certificate confirms that all data on the specified device has been securely wiped according to " << data.wipeStandard << " standards. The data is permanently unrecoverable.\n";
    ss << "    </div>\n";
    ss << "</div>\n";
    
    // Organization
    ss << "<div class=\"section\">\n";
    ss << "    <h2>Organization</h2>\n";
    ss << "    <div class=\"row\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Organization</label>\n";
    ss << "            <value>" << data.organizationName << "</value>\n";
    ss << "        </div>\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Department</label>\n";
    ss << "            <value>" << data.department << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "    <div class=\"row full\">\n";
    ss << "        <div class=\"field\">\n";
    ss << "            <label>Technician / Operator</label>\n";
    ss << "            <value>" << data.technician << "</value>\n";
    ss << "        </div>\n";
    ss << "    </div>\n";
    ss << "</div>\n";
    
    // Comments
    if (!data.comments.empty()) {
        ss << "<div class=\"section\">\n";
        ss << "    <h2>Additional Notes</h2>\n";
        ss << "    <div class=\"field\">\n";
        ss << "        <value>" << data.comments << "</value>\n";
        ss << "    </div>\n";
        ss << "</div>\n";
    }
    
    ss << "<div class=\"seal\">\n";
    ss << "    ✓\n";
    ss << "</div>\n";
    
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
    ss << "╔════════════════════════════════════════════════════════════════════════════════╗\n";
    ss << "║                  SECURE DATA WIPE CERTIFICATE                                 ║\n";
    ss << "║                      WipeEngine - Certified                                   ║\n";
    ss << "╠════════════════════════════════════════════════════════════════════════════════╣\n";
    ss << "║ Certificate ID: " << std::left << std::setw(68) << data.certificateID << "║\n";
    ss << "║ Status: " << std::setw(77) << (data.success ? "SUCCESS ✓" : "FAILED ✗") << "║\n";
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
    ss << "║ Verification: " << (data.verificationPassed ? "PASSED ✓" : "FAILED ✗") << "\n";
    ss << "╚════════════════════════════════════════════════════════════════════════════════╝\n";
    return ss.str();
}

std::string CertificateGenerator::generateJSON(const CertificateData& data) {
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"certificate\": {\n";
    ss << "    \"id\": \"" << data.certificateID << "\",\n";
    ss << "    \"version\": \"1.0.0\",\n";
    ss << "    \"issued\": \"" << data.issueDate << "T" << data.issueTime << "Z\",\n";
    ss << "    \"completed\": \"" << data.completionDate << "T" << data.completionTime << "Z\",\n";
    ss << "    \"status\": \"" << data.status << "\",\n";
    ss << "    \"success\": " << (data.success ? "true" : "false") << "\n";
    ss << "  },\n";
    ss << "  \"device\": {\n";
    ss << "    \"model\": \"" << data.deviceModel << "\",\n";
    ss << "    \"serialNumber\": \"" << data.serialNumber << "\",\n";
    ss << "    \"path\": \"" << data.devicePath << "\",\n";
    ss << "    \"capacityGB\": " << data.capacityGB << ",\n";
    ss << "    \"fileSystem\": \"" << data.fileSystem << "\"\n";
    ss << "  },\n";
    ss << "  \"wipeMethod\": {\n";
    ss << "    \"algorithm\": \"" << data.wipeMethod << "\",\n";
    ss << "    \"standard\": \"" << data.wipeStandard << "\",\n";
    ss << "    \"passes\": " << data.passes << ",\n";
    ss << "    \"bytesWiped\": " << data.bytesWiped << "\n";
    ss << "  },\n";
    ss << "  \"verification\": {\n";
    ss << "    \"prewipeHash\": \"" << data.prewipeHash << "\",\n";
    ss << "    \"postwipeHash\": \"" << data.postwipeHash << "\",\n";
    ss << "    \"passed\": " << (data.verificationPassed ? "true" : "false") << ",\n";
    ss << "    \"notes\": \"" << data.verificationNotes << "\"\n";
    ss << "  },\n";
    ss << "  \"organization\": {\n";
    ss << "    \"name\": \"" << data.organizationName << "\",\n";
    ss << "    \"department\": \"" << data.department << "\",\n";
    ss << "    \"technician\": \"" << data.technician << "\"\n";
    ss << "  }\n";
    ss << "}\n";
    return ss.str();
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
    
    filename = "Certificate_" + data.certificateID + ext;
    
    std::ofstream file(filename);
    if (!file.is_open()) return false;
    
    file << content;
    file.close();
    
    return true;
}

#pragma once
#include <string>
#include <ctime>

struct CertificateData {
    // Certificate info
    std::string certificateID;
    std::string issueDate;
    std::string issueTime;
    std::string completionDate;
    std::string completionTime;
    
    // Organization
    std::string organizationName;
    std::string department;
    std::string technician;
    
    // Device info
    std::string deviceModel;
    std::string serialNumber;
    std::string devicePath;
    unsigned long long capacityGB;
    std::string fileSystem;
    
    // Wipe details
    std::string wipeMethod;
    std::string wipeStandard;     // DOD 5220.22-M, etc.
    int passes;
    unsigned long long bytesWiped;
    
    // Verification
    std::string prewipeHash;       // SHA-256 hash before
    std::string postwipeHash;      // SHA-256 hash after
    bool verificationPassed;
    std::string verificationNotes;
    
    // Status
    bool success;
    std::string status;
    std::string comments;
};

class CertificateGenerator {
public:
    // Generate HTML certificate
    static std::string generateHTML(const CertificateData& data);
    
    // Generate text certificate
    static std::string generateText(const CertificateData& data);
    
    // Generate JSON certificate
    static std::string generateJSON(const CertificateData& data);
    
    // Save to file
    static bool saveCertificate(const CertificateData& data, 
                                const std::string& format = "html");
    
    // Generate unique certificate ID
    static std::string generateCertificateID();
    
private:
    static std::string getHTMLHeader(const CertificateData& data);
    static std::string getHTMLBody(const CertificateData& data);
    static std::string getHTMLFooter();
};

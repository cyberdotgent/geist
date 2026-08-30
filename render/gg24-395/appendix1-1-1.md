[Previous](appendix1-1.md) | [Index](README.md) | [Next](appendix1-1-2.md)

---

## APPENDIX1\.1\.1 Standards

In alphabetical order, here is a list of important standards:

- The Application Environment Specification \- AES

AES from OSF is defined as interfaces and services based on precedence order of POSIX, ANSI C, XPG3, SVID 2, BSD 4\.3\. AIX conforms with AES\.

- Andrew File System \- AFS

Networking software from Carnegie\-Mellon University\. It has been included in the Distributed Computing Environment \(DCE\) of the Open Software Foundation\(OSF\)\.

- Architecture Neutral Distribution Format \- ANDF

An OSF product whose goal is to have a single version of a UNIX application which will execute on a variety of platforms that are not necessarily compatible\. This will be accomplished after a largely automated installation process\. It is based on a product called TDF from the Electronic Division of the UK's Defense Research Agency\. The application is compiled by a "producer" which generates an architecture independent Intermediate Language \(IL\)\. This is common code for all the different platforms\. The next stage is to use an architecture specific "installer" software which will convert the IL executable code for the specific platform in which the installer runs\.

- Application Programming Interface \- API

A set of programming functions and routines that provide access between the Application layer of the OSI seven layer model and applications that want to use the network\. It is a software interface\.

- Advanced Program\-to\-Program Communication \- APPC

An implementation of the SNA's LU 6\.2 protocol that allows interconnected systems to communicate and share the processing of programs\.

- Advanced Peer\-to\-Peer Network \- APPN

An IBM SNA protocol that allows the end user to communicate across a network of interconnected nodes on a logical point\-to\-point basis without path configuration\.

- American Standard Code for Information Interchange \- ASCII

It is a widely used standard consisting of 7\-bit and 8\-bit ASCII codes\.

- Bourne Shell

The standard shell in System V versions of UNIX\. The Bourne shell is one of the AIX shells\.

- Berkeley Software Distribution \- BSD

UNIX from the University of California at Berkeley\. The latest version of BSD is 4\.3\. It has been ported to many computers, and adopted by many vendors as their standard UNIX product\. Many of the major enhancements seen in UNIX were originally developed for BSD UNIX\. Shells simplify user interactions by eliminating the user's concern with operating system requirements\.

- Common Management Information Protocol \- CMIP

OSI's application protocol for transporting network management information such as performance measurement, problem determination, configuration, accounting or security data\.

- Common Management Information Service \- CMIS

An OSI standard for network and systems management\.

- The Conference on Data Systems Languages \- CODASYL

The CODASYL COBOL committee develops and maintains the *COBOL Journal of Development*, which is used as a base for subsequent standardization\.

- Common for Open Systems \- COSE

A set of standards to be developed under an agreement by IBM, HP, SCO, SunSoft, Univel, and USL\. These standards will cover the desktop shell, networking, system administration, multimedia, graphics, and objects\. The standards will not be UNIX specific and will be open to all Open Systems and vendors\.

- C Shell

The standard shell in BSD versions of UNIX\. The C shell is included without charge in AIX\.

- Common User Access \- CUA

CUA is an IBM architecture for the user interface, first introduced with SAA in 1987\. Since then it has matured with increased focus on the graphical interface\. It influenced OSF/MOTIF\.

CUA 91 is a more object oriented interface than previous CUA guidelines\. Rather than interacting with applications, which provide access to functions the user must perform, users interact with objects that represent the inputs and outputs of their job\. It is available in OS/2 2\.0\.

- Distributed Application Environment \- DAE

DAE is a software enabler designed to allow industries and business to create applications in a distributed Client/Server environment, with a high level of portability\.

- Distributed Computing Environment \- DCE

For more information about DCE see [Chapter 6, "Distributed Computing](2-4.md) [Environment" in topic 2\.4](2-4.md)\.

- Distributed File Services \- DFS

DFS is the Andrew File System\. It makes remote files appear as local files and has been incorporated into DCE\.

- Distributed Management Environment \- DME

DME is from OSF\. DME will allow heterogeneous computer architectures on a network to be administered from a single point\. DME runs on DCE\.

- Distributed Relational Database Architecture \- DRDA

DRDA is IBM's architecture for distributed databases\. It is the architected solution for access to relational data through SQL\. Originally developed in the mid 1980s\. It was designed for large systems and networks of systems\. Recently RDA, a subset of DRDA, was accepted as an international standard through ISO\. A slightly different version was accepted by X/Open\.

- Distributed Time Services \- DTS

DTS is the component of DCE that provides a fault tolerant clock synchronization for computers connected in LANs and WANs in a distributed environment\.

- Federal Information Processing Standards \- FIPS

FIPS is a set of standards used by the Federal Government of the U\.S\.A\. to define requirements for government procurement and validation of computer systems and communications\.

- Graphics Interface Format \- GIF

GIF is a format for storing picture bitmaps\.

- Graphics Kernel System \- GKS

An ANSI standard for the device independent manipulation of two dimensional graphical data\.

- Graphical User Interface \- GUI

GUI provides multiple windows and iconic presentations\. Known GUIs include: OSF's MOTIF, Open Look, Microsoft Windows, Apple MacIntosh, and OS/2 Presentation Manager\.

- de jure standard

A formal specification developed through an official consensus process\. For example the POSIX standards\.

- Kerberos

A widely used encryption\-based authentication mechanism for network security, developed at the Massachusetts Institute of Technology \(MIT\) with extensions from HP\. It is included is OSF's DCE\. In Greek mythology \(more often called Cerberos\-its Latin name\) the 3\-headed hound of Hades, guardian of the chasm that lead to his master's kingdom\. It was brought back from the underworld by Herakles\.

- MOTIF

Usually called OSF/MOTIF\. A user interface, originally developed by OSF\. MOTIF is based on the X Windows system and is a Presentation Manager look\-alike\.

- Network File System \- NFS

NFS allows different systems \(UNIX and non\-UNIX\), architectures, or vendors connected to the same network, to access remote files as though they were local files\. The user does not have to explicitly transfer the file he wants to write or read\. The transfer is done automatically\. If the file is large, and the user only needs access to a part of the file, only the useful part will be transferred\.

- Open Systems Interconnection \- OSI

OSI is a reference model developed by ISO and IEC \(International Electrotechnical Commission\)\. It defines seven layers of communications activities and the interfaces between them, namely:

Physical

Data link

Network

Transport

Session

Presentation

Application

OSI standards are being accepted for each layer's activities and the interfaces between them\. OSI has been endorsed by many organizations, including ISO, CCITT, COSAC, UK GOSIP, and IBM\.

- Portable Application Standards Committee \- PASC

In a process to decouple POSIX from the UNIX environment, IEEE formed a new PASC committee\. PASC took over the POSIX 1003\.x definitions, and also studies other standards, such as for graphical user interfaces, and API's for OSI\.

- POSIX Conformance Document \- PCD

PCD is a conformance document that is required from system vendors for the public\. It must describe the C language environment provided by the implementation and other optional features\.

- Portable Operating System Interface \- POSIX

POSIX is a set of specifications for Open Systems\(UNIX or non\-UNIX\) elaborated by the TCOS \(Technical Committee of the IEEE Computer Society \- a consortium of users\)\. The stated purpose of POSIX is to provide an industry\-wide standard by creating a single, verifiable interface specification for a portable operating system\. POSIX specifications were initially derived from UNIX, but they are no longer dedicated to that system\. VM/ESA and MVS are expected to become POSIX compliant\.

- eXternal Data Representation \- XDR

A standard developed by SUN Microsystems\. Incorporated for representing data in machine independent format\. Used by NFS, the standard requires all data be converted into this format when using this standard, even if translations are not necessary\.

- X\.25

X\.25 defines the interface between data terminal equipment and packet switching networks\.

- X\.400

An electronic mail and messaging protocol defined by CCITT and adopted by ISO as an OSI standard\. It provides standards for the exchange of electronic mail among recipients served by diverse common carriers and computer vendors\. It is technically more advanced than the SMTP Protocol\. It supports text, graphics, teletext, videotext, and facsimile communications\.

X\.400 is more popular in Europe; in North America, SMTP is well established in the Internet\.

- X\.500 \(Directory\)

X\.500 was originally driven by X\.400 \(office\) requirements\. It was defined in 1988 by the ISO as an OSI standard \(ISO 9594\), and CCITT adopted it\. Its CCITT standard name is Directory \(X\.500\)\. There are still some significant unresolved technical issues in the 1988 standard, so a major revision is expected\. X\.500 is the standard used within OSF's DCE\.

---

[Previous](appendix1-1.md) | [Index](README.md) | [Next](appendix1-1-2.md)

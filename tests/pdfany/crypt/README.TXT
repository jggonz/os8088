tests/pdfany/crypt - the encrypted fixtures of PAGEDESK-SPEC 20.22.5 (AP-04, AP-05)

Made by:   python3 tools/os88pdfx_crypt.py tests/pdfany/crypt
With:      qpdf 12.4.2 (Homebrew), --static-id --static-aes-iv
           (and --allow-weak-crypto for the RC4 files, which qpdf 12 refuses otherwise)
From:      apps/pagedesk/samples/{HELLO,MARKUP,FLATE,PAGES,XTGUIDE,ACROFORM}.PDF

Each file is a real qpdf encryption of a sample. The RC4 and AESV2 files are byte-identical on
every run of the tool (the file key of those derives from the passwords, /O and /ID, and the IVs are
static); the R5 and R6 files (AES_256_*, GUIDE_R6) are NOT: qpdf draws their file key and salts at
random, whatever flags it is given, so the copies committed here are the reference and a fresh run
makes equivalent but different bytes (the tests do not mind). MANIFEST.TXT lists  file, base, user password ("-" = the empty one), owner password,
algorithm. The owner password of all of them is "owner"; the user password of the *_PW
files is "secret".

  RC4_40, RC4_40_PW, FORM_RC4_40    R2 / V1, 40-bit RC4 (algorithm 2 with a 5-byte key)
  RC4_128, RC4_128_PW, GUIDE_RC4    R3 / V2, 128-bit RC4
  RC4_V4                            R4 / V4 with /CFM /V2 in a crypt filter, Flate inside
  AES_128, AES_128_PW, GUIDE_AES,
  PAGES_AES                         R4 / V4 /CFM /AESV2
  AES_128_NM                        R4 AESV2 with /EncryptMetadata false
  AES_256_R5                        R5 / V5 /AESV3 (Adobe extension level 3: a plain SHA-256)
  AES_256_R6, AES_256_PW, GUIDE_R6  R6 / V5 /AESV3 (ISO 32000-2 algorithm 2.B)
  AES_256_NM                        R6 with /EncryptMetadata false

tests/pdcrypt/pdc_e2e.py regenerates them (into build/) and compares, for every file: the file
key PDXK finds with `qpdf --show-encryption-key`; the owner password too where the user
password is not empty; that a wrong password is PDE_PASSWORD; and every stream's decoded CRC,
every string and every annotation PDENG yields with those of the SAME driver over
`qpdf --decrypt`'s output.

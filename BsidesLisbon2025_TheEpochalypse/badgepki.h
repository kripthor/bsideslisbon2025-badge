#include <uECC.h>
//#define MASTERBADGE 1

char* kripthor_cert = "-----BEGIN CERTIFICATE-----\n" \
"MIIDJTCCAg2gAwIBAgIUM4W4kE8R4nxTOOJu+EoyGPA+ZzYwDQYJKoZIhvcNAQEL\n" \
"BQAwIjEgMB4GA1UEAwwXYmFkZ2VzMjAyNS5rcmlwdGhvci54eXowHhcNMjUxMTA3\n" \
"MTcxMTQ0WhcNMjYxMTA3MTcxMTQ0WjAiMSAwHgYDVQQDDBdiYWRnZXMyMDI1Lmty\n" \
"aXB0aG9yLnh5ejCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAJEIzCpJ\n" \
"spUCBfWbQTtlwkva2KN+1myScTBaMARhsxRBGKHEi3Bf5GeLhctdPMbidNNEcYxW\n" \
"xSGBAmdEYdgVWzZoguClRB7wekGwYmLGZhlW027fhuT0a62vQpBy2FVdC8up/85+\n" \
"Xm6tVYV84njIlrvjL0cH1jgXERFJKhhD92NGTzbay6VFyfSYCrFpIvz+JRXDJpIo\n" \
"zQ1HodIq9ak7ONOxxHhWl62sGVlZ6kMpQ1iDbOm+r75qpyATf5vKZ9N7JFxKyuVt\n" \
"4af49OQCTgXtxsb/4SvcQJmjBH0kdc3I85A7Algw0DsRXDixQroadVuVBYy5QxKc\n" \
"SsA5AECqaQEDZpsCAwEAAaNTMFEwHQYDVR0OBBYEFOocxKKQZfW4IWhGF24N6rWp\n" \
"Hru5MB8GA1UdIwQYMBaAFOocxKKQZfW4IWhGF24N6rWpHru5MA8GA1UdEwEB/wQF\n" \
"MAMBAf8wDQYJKoZIhvcNAQELBQADggEBAGE/dXCl/dPZHzA36+BiPjDnX3/hLNBk\n" \
"cYUsARkooeqrISNrDMg07bmZrBWkhIA5dQjLu5k3So817ecmgsdsSMgA959Y0eG3\n" \
"Mdoh+ltG7/dElhgrdLso0IF7qJ0QOOhFpmQiFv3H3Ns/WtzqrbC3E9fJsnQNpQxU\n" \
"f5kRLF+hTf9hYAAeAhuhXaK6u+i+ew1Fmtg6gdvRrNWTjZx20N4zdfIJ6rrWYOXD\n" \
"e8RpnGmoZG8FZKJ3XlI3Ub89bHwg8tMChVG/6UbkAFZppleVds+fJJNNTDsHMbZ2\n" \
"GBkGariyw1dHN9S3d2fP2FV5Ag+KV1vvf8pMDvXqD6lTAmyPB37AImk=\n" \
"-----END CERTIFICATE-----\n";

char* bsides_cert = "-----BEGIN CERTIFICATE-----\n" \ 
"MIIDejCCAmKgAwIBAgIQf+UwvzMTQ77dghYQST2KGzANBgkqhkiG9w0BAQsFADBX\n" \
"MQswCQYDVQQGEwJCRTEZMBcGA1UEChMQR2xvYmFsU2lnbiBudi1zYTEQMA4GA1UE\n" \
"CxMHUm9vdCBDQTEbMBkGA1UEAxMSR2xvYmFsU2lnbiBSb290IENBMB4XDTIzMTEx\n" \
"NTAzNDMyMVoXDTI4MDEyODAwMDA0MlowRzELMAkGA1UEBhMCVVMxIjAgBgNVBAoT\n" \
"GUdvb2dsZSBUcnVzdCBTZXJ2aWNlcyBMTEMxFDASBgNVBAMTC0dUUyBSb290IFI0\n" \
"MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAE83Rzp2iLYK5DuDXFgTB7S0md+8Fhzube\n" \
"Rr1r1WEYNa5A3XP3iZEwWus87oV8okB2O6nGuEfYKueSkWpz6bFyOZ8pn6KY019e\n" \
"WIZlD6GEZQbR3IvJx3PIjGov5cSr0R2Ko4H/MIH8MA4GA1UdDwEB/wQEAwIBhjAd\n" \
"BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwDwYDVR0TAQH/BAUwAwEB/zAd\n" \
"BgNVHQ4EFgQUgEzW63T/STaj1dj8tT7FavCUHYwwHwYDVR0jBBgwFoAUYHtmGkUN\n" \
"l8qJUC99BM00qP/8/UswNgYIKwYBBQUHAQEEKjAoMCYGCCsGAQUFBzAChhpodHRw\n" \
"Oi8vaS5wa2kuZ29vZy9nc3IxLmNydDAtBgNVHR8EJjAkMCKgIKAehhxodHRwOi8v\n" \
"Yy5wa2kuZ29vZy9yL2dzcjEuY3JsMBMGA1UdIAQMMAowCAYGZ4EMAQIBMA0GCSqG\n" \
"SIb3DQEBCwUAA4IBAQAYQrsPBtYDh5bjP2OBDwmkoWhIDDkic574y04tfzHpn+cJ\n" \
"odI2D4SseesQ6bDrarZ7C30ddLibZatoKiws3UL9xnELz4ct92vID24FfVbiI1hY\n" \
"+SW6FoVHkNeWIP0GCbaM4C6uVdF5dTUsMVs/ZbzNnIdCp5Gxmx5ejvEau8otR/Cs\n" \
"kGN+hr/W5GvT1tMBjgWKZ1i4//emhA1JG1BbPzoLJQvyEotc03lXjTaCzv8mEbep\n" \
"8RqZ7a2CPsgRbuvTPBwcOMBBmuFeU88+FSBX6+7iP0il8b4Z0QFqIwwMHfs/L6K1\n" \
"vepuoxtGzi4CZ68zJpiq1UvSqTbFJjtbD4seiMHl\n" \
"-----END CERTIFICATE-----\n" ;

//char* cert_pin = kripthor_cert;
char* cert_pin = bsides_cert;
/*
14:24:25.562 ->   Device Public Key:  0x8E, 0xA3, 0xAD, 0x8C, 0x5B, 0xAC, 0x0C, 0xBF, 0x90, 0xAC, 0xAF, 0x67, 0x50, 0xA1, 0x72, 0x2D, 0x9B, 0x53, 0x36, 0x5F, 0x07, 0x99, 0x96, 0x1A, 0xB0, 0x98, 0x1C, 0x19, 0xEB, 0x85, 0x16, 0xAE, 0x8E, 0x73, 0xFF, 0x09, 0x6B, 0x04, 0x9A, 0xB7, 0xE0, 0xEE, 0xB0, 0x77, 0xBA, 0xE9, 0x13, 0xC2, 0x96, 0x56, 0x7E, 0x3B, 0x82, 0x1E, 0x27, 0xF5, 0xF5, 0xD9, 0x38, 0x11, 0xB6, 0x5E, 0xCC, 0xE1, 
14:24:25.595 ->   Device Private Key: 0x8D, 0x0D, 0x49, 0xE2, 0xD5, 0x93, 0xEE, 0xE2, 0xDD, 0x7C, 0x9B, 0x7E, 0x60, 0x5A, 0xFC, 0xC1, 0xE5, 0xB7, 0xE7, 0xE7, 0x0B, 0x56, 0x7A, 0xF6, 0xAC, 0xD3, 0x86, 0x65, 0x4A, 0x76, 0xA8, 0xF2, 
*/

const int KEY_SIZE = 32;
const int PUB_KEY_SIZE = 64;
const int HASH_SIZE = 32; // secp256r1 works with 32-byte hashes
const int SIG_SIZE = 64;  // secp256r1 signatures are 64 bytes

#ifdef MASTERBADGE
  uint8_t pkipriv[KEY_SIZE] = {0x8D, 0x0D, 0x49, 0xE2, 0xD5, 0x93, 0xEE, 0xE2, 0xDD, 0x7C, 0x9B, 0x7E, 0x60, 0x5A, 0xFC, 0xC1, 0xE5, 0xB7, 0xE7, 0xE7, 0x0B, 0x56, 0x7A, 0xF6, 0xAC, 0xD3, 0x86, 0x65, 0x4A, 0x76, 0xA8, 0xF2};
#endif

uint8_t pkipub[PUB_KEY_SIZE] = {0x8E, 0xA3, 0xAD, 0x8C, 0x5B, 0xAC, 0x0C, 0xBF, 0x90, 0xAC, 0xAF, 0x67, 0x50, 0xA1, 0x72, 0x2D, 0x9B, 0x53, 0x36, 0x5F, 0x07, 0x99, 0x96, 0x1A, 0xB0, 0x98, 0x1C, 0x19, 0xEB, 0x85, 0x16, 0xAE, 0x8E, 0x73, 0xFF, 0x09, 0x6B, 0x04, 0x9A, 0xB7, 0xE0, 0xEE, 0xB0, 0x77, 0xBA, 0xE9, 0x13, 0xC2, 0x96, 0x56, 0x7E, 0x3B, 0x82, 0x1E, 0x27, 0xF5, 0xF5, 0xD9, 0x38, 0x11, 0xB6, 0x5E, 0xCC, 0xE1 };
uint8_t pkimessage[HASH_SIZE]; // A "hash" of the message you want to sign
uint8_t pkisig[SIG_SIZE];     // Buffer to hold the resulting signature

const struct uECC_Curve_t * pkicurve = uECC_secp256r1();

static int esp32_rng(uint8_t *dest, unsigned size) {
  while (size) {
    uint32_t random = esp_random(); // Get 32 bits (4 bytes) of random data
    uint8_t *p = (uint8_t *)&random;
    unsigned n = (size < 4) ? size : 4; 
    memcpy(dest, p, n);
    dest += n;
    size -= n;
  }
  return 1;
}

static bool pkiverify(){
  uECC_set_rng(&esp32_rng);
  return uECC_verify(pkipub, pkimessage, HASH_SIZE, pkisig, pkicurve);
}

#ifdef MASTERBADGE
static bool pkisign(){
  uECC_set_rng(&esp32_rng);
  return uECC_sign(pkipriv, pkimessage, HASH_SIZE, pkisig, pkicurve);
}
#endif


void printHex(const char* label, uint8_t* data, int len) {
  Serial.print(label);
  for (int i = 0; i < len; i++) {
    Serial.print("0x");
    if (data[i] < 0x10) {
      Serial.print("0"); // Pad with leading zero
    }
    Serial.print(data[i], HEX);
    Serial.print(", ");
  }
  Serial.println();
}

//Poly alphabetic encryption/decryption routine
//L33t H4X0r skillz...
void strstrip(char *str, int base, bool strip) {
  static const char* alphabet = "M:/q1.fKj7A-eZlV3xXoWd09QGTIaSsLg2ycbUPhuE6nRtBvO8mrpJHFwzC5NiY4Dk";
  static const int alphabet_len = 66;
  for (int i = 0; str[i] != '\0'; i++) {
    char c = str[i];
    const char* char_ptr = strchr(alphabet, c);
    if (char_ptr == NULL) {
      continue; 
    }
    int erot = base + i;
    int index = char_ptr - alphabet;
    int new_index;
    if (strip) {
      new_index = (index - (erot % alphabet_len) + alphabet_len) % alphabet_len;
    } else {
      new_index = (index + (erot % alphabet_len) + alphabet_len) % alphabet_len;
    }
    str[i] = alphabet[new_index];
  }
}

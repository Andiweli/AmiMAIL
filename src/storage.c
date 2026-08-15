#include "storage.h"
#include "buffer.h"
#include "crypto.h"
#include "tls.h"
#include "i18n.h"

#define T(de, en) amg_tr((de), (en))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <proto/dos.h>
#include <proto/amissl.h>
#include <openssl/evp.h>
#endif

#define STORAGE_ITERATIONS 100000
#define STORAGE_HEADER_V1 "AMIMAIL-ACCOUNT-1\n"
#define STORAGE_HEADER_V2 "AMIMAIL-ACCOUNT-2\n"
#define SESSION_HEADER "AMIMAIL-SESSION-KEY-1\n"
#define SESSION_KEY_SIZE 32U

static const char hex_digits[]="0123456789abcdef";

static int hex_encode(const unsigned char *data,size_t length,AmgBuffer *output)
{
    size_t i;for(i=0;i<length;++i){char pair[2]={hex_digits[data[i]>>4],hex_digits[data[i]&15U]};if(amg_buffer_append(output,pair,2U)!=AMG_OK)return AMG_ERR_MEMORY;}return AMG_OK;
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int hex_decode(const char *text,AmgBuffer *output)
{
    size_t i,length=strlen(text);if(length&1U)return AMG_ERR_PARSE;for(i=0;i<length;i+=2U){int h=hex_digit(text[i]),l=hex_digit(text[i+1U]);unsigned char value;if(h<0||l<0)return AMG_ERR_PARSE;value=(unsigned char)((h<<4)|l);if(amg_buffer_append_char(output,value)!=AMG_OK)return AMG_ERR_MEMORY;}return AMG_OK;
}

static int storage_header_version(const char *data)
{
    if (!data) return 0;
    if (!strncmp(data, STORAGE_HEADER_V2, sizeof(STORAGE_HEADER_V2) - 1U))
        return 2;
    if (!strncmp(data, STORAGE_HEADER_V1, sizeof(STORAGE_HEADER_V1) - 1U))
        return 1;
    return 0;
}

static int write_hex_line(FILE *file,const char *name,const unsigned char *data,size_t length)
{
    AmgBuffer encoded;int result;amg_buffer_init(&encoded);result=hex_encode(data,length,&encoded);if(result==AMG_OK){amg_buffer_terminate(&encoded);if(fprintf(file,"%s=%s\n",name,(char*)encoded.data)<0)result=AMG_ERR_IO;}amg_buffer_free(&encoded);return result;
}

static int replace_file(const char *temporary,const char *path)
{
#if AMIGMAIL_AMIGA
    DeleteFile((CONST_STRPTR)path);
    return Rename((CONST_STRPTR)temporary,(CONST_STRPTR)path)?AMG_OK:AMG_ERR_IO;
#else
    remove(path);
    return rename(temporary,path)==0?AMG_OK:AMG_ERR_IO;
#endif
}

static void discard_file(const char *path)
{
#if AMIGMAIL_AMIGA
    DeleteFile((CONST_STRPTR)path);
#else
    remove(path);
#endif
}

#if AMIGMAIL_AMIGA
static int encrypt_secrets(const char *master, const unsigned char *plain,
                           size_t plain_length, unsigned char salt[16],
                           unsigned char iv[12], unsigned char tag[16],
                           AmgBuffer *cipher, AmgError *error)
{
    unsigned char key[32];
    EVP_CIPHER_CTX *ctx = NULL;
    int out = 0, total = 0, result;

    memset(key, 0, sizeof(key));
    amg_error_set(error, AMG_OK, "");
    result = amg_tls_global_init(error);
    if (result != AMG_OK) return result;

    result = AMG_ERR_TLS;
    if (amg_random_bytes(salt, 16U) != AMG_OK ||
        amg_random_bytes(iv, 12U) != AMG_OK)
        goto done;
    if (PKCS5_PBKDF2_HMAC(master, (int)strlen(master), salt, 16,
                          STORAGE_ITERATIONS, EVP_sha256(), 32, key) != 1)
        goto done;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx ||
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
        goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1 ||
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, iv) != 1)
        goto done;
    if (amg_buffer_reserve(cipher, plain_length + 32U) != AMG_OK) {
        result = AMG_ERR_MEMORY;
        goto done;
    }
    if (EVP_EncryptUpdate(ctx, cipher->data, &out, plain,
                          (int)plain_length) != 1)
        goto done;
    total = out;
    if (EVP_EncryptFinal_ex(ctx, cipher->data + total, &out) != 1)
        goto done;
    total += out;
    cipher->length = (size_t)total;
    cipher->data[cipher->length] = 0;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) != 1)
        goto done;
    result = AMG_OK;

done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    amg_secure_clear(key, sizeof(key));
    amg_tls_global_cleanup();
    if (result == AMG_ERR_MEMORY)
        amg_error_set(error, result, T("Nicht genug Speicher.", "Not enough memory."));
    else if (result != AMG_OK)
        amg_error_set(error, result,
                      T("AmiSSL konnte die Kontodaten nicht verschl\303\274sseln.", "AmiSSL could not encrypt the account data."));
    return result;
}

static int derive_storage_key(const char *master, const unsigned char salt[16],
                              unsigned char key[SESSION_KEY_SIZE],
                              AmgError *error)
{
    int result;
    if (!master || !*master || !salt || !key) return AMG_ERR_ARGUMENT;
    memset(key, 0, SESSION_KEY_SIZE);
    result = amg_tls_global_init(error);
    if (result != AMG_OK) return result;
    if (PKCS5_PBKDF2_HMAC(master, (int)strlen(master), salt, 16,
                          STORAGE_ITERATIONS, EVP_sha256(),
                          (int)SESSION_KEY_SIZE, key) != 1) {
        amg_tls_global_cleanup();
        amg_secure_clear(key, SESSION_KEY_SIZE);
        amg_error_set(error, AMG_ERR_TLS,
                      T("AmiSSL konnte den Sitzungsschlüssel nicht ableiten.",
                        "AmiSSL could not derive the session key."));
        return AMG_ERR_TLS;
    }
    amg_tls_global_cleanup();
    return AMG_OK;
}

static int decrypt_secrets_with_key(const unsigned char key[SESSION_KEY_SIZE],
                                    const unsigned char *cipher,
                                    size_t cipher_length,
                                    const unsigned char iv[12],
                                    const unsigned char tag[16],
                                    AmgBuffer *plain, AmgError *error)
{
    EVP_CIPHER_CTX *ctx = NULL;
    int out = 0, total = 0, result;
    if (!key || !cipher || !iv || !tag || !plain) return AMG_ERR_ARGUMENT;
    amg_error_set(error, AMG_OK, "");
    result = amg_tls_global_init(error);
    if (result != AMG_OK) return result;
    result = AMG_ERR_AUTH;
    ctx = EVP_CIPHER_CTX_new();
    if (!ctx ||
        EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1)
        goto done;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) != 1 ||
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, iv) != 1)
        goto done;
    if (amg_buffer_reserve(plain, cipher_length + 1U) != AMG_OK) {
        result = AMG_ERR_MEMORY;
        goto done;
    }
    if (EVP_DecryptUpdate(ctx, plain->data, &out, cipher,
                          (int)cipher_length) != 1)
        goto done;
    total = out;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *)tag) != 1 ||
        EVP_DecryptFinal_ex(ctx, plain->data + total, &out) != 1)
        goto done;
    total += out;
    plain->length = (size_t)total;
    plain->data[plain->length] = 0;
    result = AMG_OK;

done:
    if (ctx) EVP_CIPHER_CTX_free(ctx);
    amg_tls_global_cleanup();
    if (result == AMG_ERR_MEMORY)
        amg_error_set(error, result, T("Nicht genug Speicher.", "Not enough memory."));
    return result;
}

static int decrypt_secrets(const char *master, const unsigned char *cipher,
                           size_t cipher_length, const unsigned char salt[16],
                           const unsigned char iv[12],
                           const unsigned char tag[16], AmgBuffer *plain,
                           AmgError *error)
{
    unsigned char key[SESSION_KEY_SIZE];
    int result;
    memset(key, 0, sizeof(key));
    result = derive_storage_key(master, salt, key, error);
    if (result == AMG_OK)
        result = decrypt_secrets_with_key(key, cipher, cipher_length,
                                          iv, tag, plain, error);
    amg_secure_clear(key, sizeof(key));
    return result;
}
#endif

int amg_storage_save_account(const char *path,const AmgAccount *account,const char *master_password,AmgError *error)
{
    char temporary[512];FILE *file;int result=AMG_OK;AmgBuffer plain,cipher;
#if AMIGMAIL_AMIGA
    unsigned char salt[16],iv[12],tag[16];
#endif
    amg_error_set(error,AMG_OK,"");
    if (!path || !account) return AMG_ERR_ARGUMENT;
    if (strlen(path) + 5U >= sizeof(temporary)) return AMG_ERR_LIMIT;
    snprintf(temporary,sizeof(temporary),"%s.new",path);
    file=fopen(temporary,"wb");if(!file){amg_error_set(error,AMG_ERR_IO,T("Kontodatei konnte nicht geschrieben werden.", "Account file could not be written."));return AMG_ERR_IO;}
    amg_buffer_init(&plain);amg_buffer_init(&cipher);fprintf(file,"%s",STORAGE_HEADER_V2);
    if(write_hex_line(file,"display_name",(const unsigned char*)account->display_name,strlen(account->display_name))!=AMG_OK||
       write_hex_line(file,"email",(const unsigned char*)account->email,strlen(account->email))!=AMG_OK||
       write_hex_line(file,"imap_username",(const unsigned char*)account->imap_username,strlen(account->imap_username))!=AMG_OK||
       write_hex_line(file,"smtp_username",(const unsigned char*)account->smtp_username,strlen(account->smtp_username))!=AMG_OK||
       write_hex_line(file,"folder_sent",(const unsigned char*)account->sent_mailbox,strlen(account->sent_mailbox))!=AMG_OK||
       write_hex_line(file,"folder_drafts",(const unsigned char*)account->drafts_mailbox,strlen(account->drafts_mailbox))!=AMG_OK||
       write_hex_line(file,"folder_all",(const unsigned char*)account->all_mailbox,strlen(account->all_mailbox))!=AMG_OK||
       write_hex_line(file,"folder_spam",(const unsigned char*)account->spam_mailbox,strlen(account->spam_mailbox))!=AMG_OK||
       write_hex_line(file,"folder_trash",(const unsigned char*)account->trash_mailbox,strlen(account->trash_mailbox))!=AMG_OK||
       fprintf(file,"auth_mode=%d\nimap_host=%s\nimap_port=%u\nimap_starttls=%d\nsmtp_host=%s\nsmtp_port=%u\nsmtp_starttls=%d\nsave_sent_copy=%d\nfetch_on_start=%d\nperiodic_fetch=%d\nfetch_days=%u\nnotification_sound=%d\n",
       (int)account->auth_mode,account->imap_host,(unsigned)account->imap_port,account->imap_starttls,account->smtp_host,(unsigned)account->smtp_port,account->smtp_starttls,account->save_sent_copy?1:0,account->fetch_on_start?1:0,account->periodic_fetch?1:0,account->fetch_days?account->fetch_days:180U,account->notification_sound?1:0)<0 ||
       write_hex_line(file,"notification_sound_path",(const unsigned char*)account->notification_sound_path,strlen(account->notification_sound_path))!=AMG_OK)result=AMG_ERR_IO;
    if(result==AMG_OK&&master_password&&*master_password){
        amg_buffer_append_cstr(&plain,"imap_password=");if(account->imap_password)hex_encode((unsigned char*)account->imap_password,strlen(account->imap_password),&plain);
        amg_buffer_append_cstr(&plain,"\nsmtp_password=");if(account->smtp_password)hex_encode((unsigned char*)account->smtp_password,strlen(account->smtp_password),&plain);
        amg_buffer_append_cstr(&plain,"\nrefresh_token=");if(account->refresh_token)hex_encode((unsigned char*)account->refresh_token,strlen(account->refresh_token),&plain);amg_buffer_append_char(&plain,'\n');
#if AMIGMAIL_AMIGA
        if(result==AMG_OK)result=encrypt_secrets(master_password,plain.data,plain.length,salt,iv,tag,&cipher,error);
        if(result==AMG_OK){fprintf(file,"secrets=aes-256-gcm\niterations=%d\n",STORAGE_ITERATIONS);result=write_hex_line(file,"salt",salt,16U);}
        if(result==AMG_OK)result=write_hex_line(file,"iv",iv,12U);
        if(result==AMG_OK)result=write_hex_line(file,"tag",tag,16U);
        if(result==AMG_OK)result=write_hex_line(file,"ciphertext",cipher.data,cipher.length);
#else
        result=AMG_ERR_UNSUPPORTED;
#endif
    }else if(result==AMG_OK)fprintf(file,"secrets=session-only\n");
    amg_secure_clear(plain.data,plain.capacity);amg_secure_clear(cipher.data,cipher.capacity);amg_buffer_free(&plain);amg_buffer_free(&cipher);
    if (fclose(file) != 0 && result == AMG_OK) result = AMG_ERR_IO;
    if(result==AMG_OK)result=replace_file(temporary,path);
    if(result!=AMG_OK)discard_file(temporary);
    if(result==AMG_OK)amg_error_set(error,AMG_OK,"");
    else if(!error||error->code==AMG_OK)
        amg_error_set(error,result,T("Kontodatei konnte nicht sicher gespeichert werden.", "Account file could not be saved securely."));
    return result;
}

static char *read_all(const char *path,size_t *length)
{
    FILE *file=fopen(path,"rb");long size;char *data;if(!file)return NULL;if(fseek(file,0,SEEK_END)||((size=ftell(file))<0)||fseek(file,0,SEEK_SET)){fclose(file);return NULL;}
    data=(char*)malloc((size_t)size+1U);if(!data){fclose(file);return NULL;}if(fread(data,1U,(size_t)size,file)!=(size_t)size){free(data);fclose(file);return NULL;}fclose(file);data[size]=0;*length=(size_t)size;return data;
}

static const char *field(const char *data,const char *name,char *value,size_t size)
{
    size_t n=strlen(name);const char *p=data;while((p=strstr(p,name))!=NULL){if((p==data||p[-1]=='\n')&&p[n]=='='){const char *start=p+n+1U,*end=strchr(start,'\n');size_t len;if(!end)end=start+strlen(start);len=(size_t)(end-start);if(len&&start[len-1U]=='\r')--len;if(len>=size)len=size-1U;memcpy(value,start,len);value[len]=0;return value;}p+=n;}return NULL;
}

int amg_storage_load_legacy_master(const char *path, char *output,
                                       size_t capacity)
{
    size_t length;
    char *data;
    char value[512];
    AmgBuffer decoded;
    int result = AMG_ERR_IO;
    if (!path || !output || capacity < 2U) return AMG_ERR_ARGUMENT;
    output[0] = 0;
    data = read_all(path, &length);
    (void)length;
    if (!data) return AMG_ERR_IO;
    amg_buffer_init(&decoded);
    if (storage_header_version(data) == 1 &&
        field(data, "remembered_master", value, sizeof(value)) &&
        hex_decode(value, &decoded) == AMG_OK &&
        decoded.length < capacity) {
        memcpy(output, decoded.data, decoded.length);
        output[decoded.length] = 0;
        result = AMG_OK;
    }
    amg_secure_clear(decoded.data, decoded.capacity);
    amg_buffer_free(&decoded);
    amg_secure_clear(data, strlen(data));
    free(data);
    return result;
}

#if AMIGMAIL_AMIGA
static int read_account_salt(const char *path, unsigned char salt[16],
                             AmgError *error)
{
    size_t length;
    char *data;
    char value[256];
    AmgBuffer decoded;
    int result = AMG_ERR_PARSE;
    if (!path || !salt) return AMG_ERR_ARGUMENT;
    data = read_all(path, &length);
    (void)length;
    if (!data) {
        amg_error_set(error, AMG_ERR_IO,
                      T("Kontodatei wurde nicht gefunden.",
                        "Account file was not found."));
        return AMG_ERR_IO;
    }
    amg_buffer_init(&decoded);
    if (storage_header_version(data) == 2 &&
        field(data, "secrets", value, sizeof(value)) &&
        !strcmp(value, "aes-256-gcm") &&
        field(data, "salt", value, sizeof(value)) &&
        hex_decode(value, &decoded) == AMG_OK && decoded.length == 16U) {
        memcpy(salt, decoded.data, 16U);
        result = AMG_OK;
    }
    amg_secure_clear(decoded.data, decoded.capacity);
    amg_buffer_free(&decoded);
    amg_secure_clear(data, strlen(data));
    free(data);
    if (result != AMG_OK)
        amg_error_set(error, result,
                      T("Kontodatei enthält keinen gültigen Verschlüsselungsschlüssel.",
                        "Account file does not contain valid encryption data."));
    return result;
}
#endif

void amg_storage_forget_session_key(const char *session_path)
{
    if (session_path && *session_path) discard_file(session_path);
}

int amg_storage_cache_session_key(const char *account_path,
                                  const char *session_path,
                                  const char *master_password,
                                  AmgError *error)
{
#if AMIGMAIL_AMIGA
    unsigned char salt[16];
    unsigned char key[SESSION_KEY_SIZE];
    char temporary[512];
    FILE *file = NULL;
    int result;
    temporary[0] = 0;
    if (!account_path || !session_path || !master_password ||
        !*master_password)
        return AMG_ERR_ARGUMENT;
    memset(salt, 0, sizeof(salt));
    memset(key, 0, sizeof(key));
    result = read_account_salt(account_path, salt, error);
    if (result == AMG_OK)
        result = derive_storage_key(master_password, salt, key, error);
    if (result == AMG_OK) {
        if (strlen(session_path) + 5U >= sizeof(temporary))
            result = AMG_ERR_LIMIT;
        else {
            snprintf(temporary, sizeof(temporary), "%s.new", session_path);
            file = fopen(temporary, "wb");
            if (!file) result = AMG_ERR_IO;
        }
    }
    if (result == AMG_OK && fprintf(file, "%s", SESSION_HEADER) < 0)
        result = AMG_ERR_IO;
    if (result == AMG_OK)
        result = write_hex_line(file, "salt", salt, sizeof(salt));
    if (result == AMG_OK)
        result = write_hex_line(file, "key", key, sizeof(key));
    if (file && fclose(file) != 0 && result == AMG_OK) result = AMG_ERR_IO;
    if (result == AMG_OK) result = replace_file(temporary, session_path);
    else if (temporary[0]) discard_file(temporary);
    amg_secure_clear(key, sizeof(key));
    amg_secure_clear(salt, sizeof(salt));
    if (result == AMG_OK) amg_error_set(error, AMG_OK, "");
    else if (!error || error->code == AMG_OK)
        amg_error_set(error, result,
                      T("Sitzungsschlüssel konnte nicht in ENV: gespeichert werden.",
                        "Session key could not be stored in ENV:."));
    return result;
#else
    (void)account_path; (void)session_path; (void)master_password;
    amg_error_set(error, AMG_ERR_UNSUPPORTED,
                  T("Sitzungsschlüssel werden nur unter AmigaOS verwendet.",
                    "Session keys are only used on AmigaOS."));
    return AMG_ERR_UNSUPPORTED;
#endif
}

static int load_account_internal(const char *path, const char *master_password,
                                 const unsigned char *session_key,
                                 AmgAccount *account, AmgError *error)
{
    size_t length;
    char *data, *line;
    char value[2048];
    AmgBuffer decoded;
    int result = AMG_OK;
    amg_error_set(error, AMG_OK, "");
    if (!path || !account) return AMG_ERR_ARGUMENT;
    data = read_all(path, &length);
    (void)length;
    if (!data) {
        amg_error_set(error, AMG_ERR_IO,
                      T("Kontodatei wurde nicht gefunden.",
                        "Account file was not found."));
        return AMG_ERR_IO;
    }
    if (!storage_header_version(data)) {
        free(data);
        amg_error_set(error, AMG_ERR_PARSE,
                      T("Kontodatei ist ungültig.", "Account file is invalid."));
        return AMG_ERR_PARSE;
    }
    amg_account_init(account);
    amg_buffer_init(&decoded);
#define LOAD_HEX_STRING(field_name, destination) do { \
    decoded.length = 0; \
    if (field(data, (field_name), value, sizeof(value)) && \
        hex_decode(value, &decoded) == AMG_OK) { \
        amg_buffer_terminate(&decoded); \
        snprintf((destination), sizeof(destination), "%s", (char *)decoded.data); \
    } \
} while (0)
    LOAD_HEX_STRING("display_name", account->display_name);
    LOAD_HEX_STRING("email", account->email);
    if (field(data, "auth_mode", value, sizeof(value)))
        account->auth_mode = (AmgAuthMode)atoi(value);
    LOAD_HEX_STRING("imap_username", account->imap_username);
    LOAD_HEX_STRING("smtp_username", account->smtp_username);
    LOAD_HEX_STRING("folder_sent", account->sent_mailbox);
    LOAD_HEX_STRING("folder_drafts", account->drafts_mailbox);
    LOAD_HEX_STRING("folder_all", account->all_mailbox);
    LOAD_HEX_STRING("folder_spam", account->spam_mailbox);
    LOAD_HEX_STRING("folder_trash", account->trash_mailbox);
#undef LOAD_HEX_STRING
    if (field(data, "imap_host", value, sizeof(value))) {
        strncpy(account->imap_host, value, sizeof(account->imap_host) - 1U);
        account->imap_host[sizeof(account->imap_host) - 1U] = 0;
    }
    if (field(data, "imap_port", value, sizeof(value)))
        account->imap_port = (unsigned short)atoi(value);
    if (field(data, "imap_starttls", value, sizeof(value)))
        account->imap_starttls = atoi(value) ? 1 : 0;
    if (field(data, "smtp_host", value, sizeof(value))) {
        strncpy(account->smtp_host, value, sizeof(account->smtp_host) - 1U);
        account->smtp_host[sizeof(account->smtp_host) - 1U] = 0;
    }
    if (field(data, "smtp_port", value, sizeof(value)))
        account->smtp_port = (unsigned short)atoi(value);
    if (field(data, "smtp_starttls", value, sizeof(value)))
        account->smtp_starttls = atoi(value) ? 1 : 0;
    if (field(data, "save_sent_copy", value, sizeof(value)))
        account->save_sent_copy = atoi(value) ? 1 : 0;
    if (field(data, "fetch_on_start", value, sizeof(value)))
        account->fetch_on_start = atoi(value) ? 1 : 0;
    if (field(data, "periodic_fetch", value, sizeof(value)))
        account->periodic_fetch = atoi(value) ? 1 : 0;
    if (field(data, "fetch_days", value, sizeof(value))) {
        unsigned long days = strtoul(value, NULL, 10);
        if (days >= 1UL && days <= 3650UL)
            account->fetch_days = (unsigned int)days;
    }
    if (field(data, "notification_sound", value, sizeof(value)))
        account->notification_sound = atoi(value) ? 1 : 0;
    decoded.length = 0;
    if (field(data, "notification_sound_path", value, sizeof(value)) &&
        hex_decode(value, &decoded) == AMG_OK) {
        amg_buffer_terminate(&decoded);
        snprintf(account->notification_sound_path,
                 sizeof(account->notification_sound_path), "%s",
                 (char *)decoded.data);
    }
    if (field(data, "secrets", value, sizeof(value)) &&
        !strcmp(value, "aes-256-gcm")) {
        AmgBuffer salt, iv, tag, cipher, plain;
        amg_buffer_init(&salt); amg_buffer_init(&iv); amg_buffer_init(&tag);
        amg_buffer_init(&cipher); amg_buffer_init(&plain);
        if (!field(data, "salt", value, sizeof(value)) ||
            hex_decode(value, &salt) != AMG_OK ||
            !field(data, "iv", value, sizeof(value)) ||
            hex_decode(value, &iv) != AMG_OK ||
            !field(data, "tag", value, sizeof(value)) ||
            hex_decode(value, &tag) != AMG_OK ||
            !field(data, "ciphertext", value, sizeof(value)) ||
            hex_decode(value, &cipher) != AMG_OK ||
            salt.length != 16U || iv.length != 12U || tag.length != 16U) {
            result = AMG_ERR_PARSE;
        }
#if AMIGMAIL_AMIGA
        else if (session_key) {
            result = decrypt_secrets_with_key(session_key, cipher.data,
                                              cipher.length, iv.data, tag.data,
                                              &plain, error);
        } else if (master_password && *master_password) {
            result = decrypt_secrets(master_password, cipher.data,
                                     cipher.length, salt.data, iv.data,
                                     tag.data, &plain, error);
        } else {
            result = AMG_ERR_AUTH;
        }
#else
        else if (master_password || session_key) result = AMG_ERR_UNSUPPORTED;
        else result = AMG_ERR_AUTH;
#endif
        if (result == AMG_OK) {
            amg_buffer_terminate(&plain);
            line = (char *)plain.data;
            if (field(line, "imap_password", value, sizeof(value))) {
                decoded.length = 0;
                if (hex_decode(value, &decoded) == AMG_OK) {
                    amg_buffer_terminate(&decoded);
                    amg_account_set_secret(&account->imap_password,
                                           (char *)decoded.data);
                }
            }
            if (field(line, "smtp_password", value, sizeof(value))) {
                decoded.length = 0;
                if (hex_decode(value, &decoded) == AMG_OK) {
                    amg_buffer_terminate(&decoded);
                    amg_account_set_secret(&account->smtp_password,
                                           (char *)decoded.data);
                }
            }
            if (field(line, "refresh_token", value, sizeof(value))) {
                decoded.length = 0;
                if (hex_decode(value, &decoded) == AMG_OK) {
                    amg_buffer_terminate(&decoded);
                    amg_account_set_secret(&account->refresh_token,
                                           (char *)decoded.data);
                }
            }
        }
        amg_secure_clear(plain.data, plain.capacity);
        amg_buffer_free(&salt); amg_buffer_free(&iv); amg_buffer_free(&tag);
        amg_buffer_free(&cipher); amg_buffer_free(&plain);
    }
    if (result == AMG_OK) amg_account_normalize(account);
    amg_secure_clear(decoded.data, decoded.capacity);
    amg_buffer_free(&decoded);
    amg_secure_clear(data, strlen(data));
    free(data);
    if (result == AMG_OK) amg_error_set(error, AMG_OK, "");
    else if (result == AMG_ERR_AUTH)
        amg_error_set(error, result,
                      T("Master-Passwort fehlt oder ist falsch.",
                        "Master password is missing or incorrect."));
    else if (!error || error->code == AMG_OK)
        amg_error_set(error, result,
                      T("Kontodatei ist ungültig.", "Account file is invalid."));
    return result;
}

int amg_storage_load_account(const char *path, const char *master_password,
                             AmgAccount *account, AmgError *error)
{
    return load_account_internal(path, master_password, NULL, account, error);
}

int amg_storage_load_account_session(const char *account_path,
                                     const char *session_path,
                                     AmgAccount *account, AmgError *error)
{
#if AMIGMAIL_AMIGA
    size_t length;
    char *data;
    char value[256];
    AmgBuffer decoded;
    unsigned char cached_salt[16], account_salt[16], key[SESSION_KEY_SIZE];
    int result = AMG_ERR_AUTH;
    if (!account_path || !session_path || !account) return AMG_ERR_ARGUMENT;
    memset(cached_salt, 0, sizeof(cached_salt));
    memset(account_salt, 0, sizeof(account_salt));
    memset(key, 0, sizeof(key));
    data = read_all(session_path, &length);
    (void)length;
    if (!data) {
        amg_error_set(error, AMG_ERR_AUTH,
                      T("Für diese Amiga-Sitzung ist das Konto noch gesperrt.",
                        "The account is not yet unlocked for this Amiga session."));
        return AMG_ERR_AUTH;
    }
    amg_buffer_init(&decoded);
    if (strncmp(data, SESSION_HEADER, sizeof(SESSION_HEADER) - 1U) != 0)
        goto done;
    if (!field(data, "salt", value, sizeof(value)) ||
        hex_decode(value, &decoded) != AMG_OK || decoded.length != 16U)
        goto done;
    memcpy(cached_salt, decoded.data, 16U);
    decoded.length = 0;
    if (!field(data, "key", value, sizeof(value)) ||
        hex_decode(value, &decoded) != AMG_OK ||
        decoded.length != SESSION_KEY_SIZE)
        goto done;
    memcpy(key, decoded.data, SESSION_KEY_SIZE);
    result = read_account_salt(account_path, account_salt, error);
    if (result != AMG_OK || memcmp(cached_salt, account_salt, 16U)) {
        result = AMG_ERR_AUTH;
        goto done;
    }
    result = load_account_internal(account_path, NULL, key, account, error);

done:
    amg_secure_clear(key, sizeof(key));
    amg_secure_clear(cached_salt, sizeof(cached_salt));
    amg_secure_clear(account_salt, sizeof(account_salt));
    amg_secure_clear(decoded.data, decoded.capacity);
    amg_buffer_free(&decoded);
    amg_secure_clear(data, strlen(data));
    free(data);
    if (result != AMG_OK) {
        amg_storage_forget_session_key(session_path);
        if (result == AMG_ERR_AUTH)
            amg_error_set(error, result,
                          T("Sitzungsschlüssel ist abgelaufen oder ungültig.",
                            "Session key has expired or is invalid."));
    }
    return result;
#else
    (void)account_path; (void)session_path; (void)account;
    amg_error_set(error, AMG_ERR_UNSUPPORTED,
                  T("Sitzungsschlüssel werden nur unter AmigaOS verwendet.",
                    "Session keys are only used on AmigaOS."));
    return AMG_ERR_UNSUPPORTED;
#endif
}


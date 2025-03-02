// Copyright (c) 2017 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.
//
// ---------------------------------------------------------------------------
//
// This file was generated by the CEF translator tool. If making changes by
// hand only do so within the body of existing method and function
// implementations. See the translator.README.txt file in the tools directory
// for more information.
//

#include <algorithm>
#include "libcef_dll/cpptoc/binary_value_cpptoc.h"
#include "libcef_dll/cpptoc/x509cert_principal_cpptoc.h"
#include "libcef_dll/cpptoc/x509certificate_cpptoc.h"


namespace {

// MEMBER FUNCTIONS - Body may be edited by hand.

cef_x509cert_principal_t* CEF_CALLBACK x509certificate_get_subject(
    struct _cef_x509certificate_t* self) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return NULL;

  // Execute
  CefRefPtr<CefX509CertPrincipal> _retval = CefX509CertificateCppToC::Get(
      self)->GetSubject();

  // Return type: refptr_same
  return CefX509CertPrincipalCppToC::Wrap(_retval);
}

cef_x509cert_principal_t* CEF_CALLBACK x509certificate_get_issuer(
    struct _cef_x509certificate_t* self) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return NULL;

  // Execute
  CefRefPtr<CefX509CertPrincipal> _retval = CefX509CertificateCppToC::Get(
      self)->GetIssuer();

  // Return type: refptr_same
  return CefX509CertPrincipalCppToC::Wrap(_retval);
}

cef_binary_value_t* CEF_CALLBACK x509certificate_get_serial_number(
    struct _cef_x509certificate_t* self) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return NULL;

  // Execute
  CefRefPtr<CefBinaryValue> _retval = CefX509CertificateCppToC::Get(
      self)->GetSerialNumber();

  // Return type: refptr_same
  return CefBinaryValueCppToC::Wrap(_retval);
}

cef_time_t CEF_CALLBACK x509certificate_get_valid_start(
    struct _cef_x509certificate_t* self) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return CefTime();

  // Execute
  cef_time_t _retval = CefX509CertificateCppToC::Get(self)->GetValidStart();

  // Return type: simple
  return _retval;
}

cef_time_t CEF_CALLBACK x509certificate_get_valid_expiry(
    struct _cef_x509certificate_t* self) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return CefTime();

  // Execute
  cef_time_t _retval = CefX509CertificateCppToC::Get(self)->GetValidExpiry();

  // Return type: simple
  return _retval;
}

cef_binary_value_t* CEF_CALLBACK x509certificate_get_derencoded(
    struct _cef_x509certificate_t* self) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return NULL;

  // Execute
  CefRefPtr<CefBinaryValue> _retval = CefX509CertificateCppToC::Get(
      self)->GetDEREncoded();

  // Return type: refptr_same
  return CefBinaryValueCppToC::Wrap(_retval);
}

cef_binary_value_t* CEF_CALLBACK x509certificate_get_pemencoded(
    struct _cef_x509certificate_t* self) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return NULL;

  // Execute
  CefRefPtr<CefBinaryValue> _retval = CefX509CertificateCppToC::Get(
      self)->GetPEMEncoded();

  // Return type: refptr_same
  return CefBinaryValueCppToC::Wrap(_retval);
}

size_t CEF_CALLBACK x509certificate_get_issuer_chain_size(
    struct _cef_x509certificate_t* self) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return 0;

  // Execute
  size_t _retval = CefX509CertificateCppToC::Get(self)->GetIssuerChainSize();

  // Return type: simple
  return _retval;
}

void CEF_CALLBACK x509certificate_get_derencoded_issuer_chain(
    struct _cef_x509certificate_t* self, size_t* chainCount,
    cef_binary_value_t** chain) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return;
  // Verify param: chain; type: refptr_vec_same_byref
  DCHECK(chainCount && (*chainCount == 0 || chain));
  if (!chainCount || (*chainCount > 0 && !chain))
    return;

  // Translate param: chain; type: refptr_vec_same_byref
  std::vector<CefRefPtr<CefBinaryValue> > chainList;
  if (chainCount && *chainCount > 0 && chain) {
    for (size_t i = 0; i < *chainCount; ++i) {
      chainList.push_back(CefBinaryValueCppToC::Unwrap(chain[i]));
    }
  }

  // Execute
  CefX509CertificateCppToC::Get(self)->GetDEREncodedIssuerChain(
      chainList);

  // Restore param: chain; type: refptr_vec_same_byref
  if (chainCount && chain) {
    *chainCount = std::min(chainList.size(), *chainCount);
    if (*chainCount > 0) {
      for (size_t i = 0; i < *chainCount; ++i) {
        chain[i] = CefBinaryValueCppToC::Wrap(chainList[i]);
      }
    }
  }
}

void CEF_CALLBACK x509certificate_get_pemencoded_issuer_chain(
    struct _cef_x509certificate_t* self, size_t* chainCount,
    cef_binary_value_t** chain) {
  // AUTO-GENERATED CONTENT - DELETE THIS COMMENT BEFORE MODIFYING

  DCHECK(self);
  if (!self)
    return;
  // Verify param: chain; type: refptr_vec_same_byref
  DCHECK(chainCount && (*chainCount == 0 || chain));
  if (!chainCount || (*chainCount > 0 && !chain))
    return;

  // Translate param: chain; type: refptr_vec_same_byref
  std::vector<CefRefPtr<CefBinaryValue> > chainList;
  if (chainCount && *chainCount > 0 && chain) {
    for (size_t i = 0; i < *chainCount; ++i) {
      chainList.push_back(CefBinaryValueCppToC::Unwrap(chain[i]));
    }
  }

  // Execute
  CefX509CertificateCppToC::Get(self)->GetPEMEncodedIssuerChain(
      chainList);

  // Restore param: chain; type: refptr_vec_same_byref
  if (chainCount && chain) {
    *chainCount = std::min(chainList.size(), *chainCount);
    if (*chainCount > 0) {
      for (size_t i = 0; i < *chainCount; ++i) {
        chain[i] = CefBinaryValueCppToC::Wrap(chainList[i]);
      }
    }
  }
}

}  // namespace


// CONSTRUCTOR - Do not edit by hand.

CefX509CertificateCppToC::CefX509CertificateCppToC() {
  GetStruct()->get_subject = x509certificate_get_subject;
  GetStruct()->get_issuer = x509certificate_get_issuer;
  GetStruct()->get_serial_number = x509certificate_get_serial_number;
  GetStruct()->get_valid_start = x509certificate_get_valid_start;
  GetStruct()->get_valid_expiry = x509certificate_get_valid_expiry;
  GetStruct()->get_derencoded = x509certificate_get_derencoded;
  GetStruct()->get_pemencoded = x509certificate_get_pemencoded;
  GetStruct()->get_issuer_chain_size = x509certificate_get_issuer_chain_size;
  GetStruct()->get_derencoded_issuer_chain =
      x509certificate_get_derencoded_issuer_chain;
  GetStruct()->get_pemencoded_issuer_chain =
      x509certificate_get_pemencoded_issuer_chain;
}

template<> CefRefPtr<CefX509Certificate> CefCppToC<CefX509CertificateCppToC,
    CefX509Certificate, cef_x509certificate_t>::UnwrapDerived(
    CefWrapperType type, cef_x509certificate_t* s) {
  NOTREACHED() << "Unexpected class type: " << type;
  return NULL;
}

#if DCHECK_IS_ON()
template<> base::AtomicRefCount CefCppToC<CefX509CertificateCppToC,
    CefX509Certificate, cef_x509certificate_t>::DebugObjCt = 0;
#endif

template<> CefWrapperType CefCppToC<CefX509CertificateCppToC,
    CefX509Certificate, cef_x509certificate_t>::kWrapperType =
    WT_X509CERTIFICATE;

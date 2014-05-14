/*!
 * \file edns.h
 *
 * \author Lubos Slovak <lubos.slovak@nic.cz>
 *
 * \brief Functions for manipulating the EDNS OPT pseudo-RR and EDNS server
 *        parameters.
 *
 * \addtogroup libknot
 * @{
 */
/*  Copyright (C) 2014 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _KNOT_EDNS_H_
#define _KNOT_EDNS_H_

#include <stdint.h>

#include "libknot/util/utils.h"
#include "libknot/rrset.h"

/* Forward declaration. */
struct knot_packet;

/*! \brief Structure holding basic EDNS parameters of the server. */
struct knot_edns_params {
	uint16_t payload;    /*!< Max UDP payload. */
	uint8_t version;     /*!< Supported version of EDNS. */
	uint16_t nsid_len;   /*!< Length of NSID string. */
	uint8_t *nsid;       /*!< NSID string. */
	uint16_t flags;      /*!< EDNS flags. Store in wire byte order. */
};

typedef struct knot_edns_params knot_edns_params_t;

/*! \brief Various constants related to EDNS. */
enum knot_edns_const {
	/*! \brief Minimal UDP payload with EDNS enabled. */
	KNOT_EDNS_MIN_UDP_PAYLOAD = 512,
	/*! \brief Minimal payload when using DNSSEC (RFC4035/sec.3) */
	KNOT_EDNS_MIN_DNSSEC_PAYLOAD = 1220,
	/*! \brief Maximal UDP payload with EDNS enabled. */
	KNOT_EDNS_MAX_UDP_PAYLOAD = 4096,
	/*! \brief Supported EDNS version. */
	KNOT_EDNS_VERSION = 0,
	/*! \brief Default EDNS flags to be set in OPT RR. */
	KNOT_EDNS_DEFAULT_FLAGS = 0,
	/*! \brief NSID option code. */
	KNOT_EDNS_OPTION_NSID     = 3,
	/*! \brief Minimum size of EDNS OPT RR in wire format. */
	KNOT_EDNS_MIN_SIZE        = 11
};

/*! \brief Enumeration of named options. */
enum knot_edns_option {
	KNOT_PKT_EDNS_PAYLOAD = 0,
	KNOT_PKT_EDNS_VERSION = 1,
	KNOT_PKT_EDNS_RCODE   = 2,
	KNOT_PKT_EDNS_FLAG_DO = 3,
	KNOT_PKT_EDNS_NSID    = 4
};

/*! \brief EDNS flags.
 *
 * \note Use only with unsigned 2-byte variables.
 * \warning Flags are represented in machine byte order.
 */
enum knot_edns_flags {
	KNOT_EDNS_FLAG_DO = (uint16_t)1 << 15
};

/*----------------------------------------------------------------------------*/
/* EDNS server parameters handling functions                                  */
/*----------------------------------------------------------------------------*/
/*!
 * \brief Creates new structure for holding server's EDNS parameters.
 *
 * \param max_payload  Max UDP payload.
 * \param ver          EDNS version.
 * \param flags        Flags (in wire byte order).
 * \param nsid_len     Length of the NSID string. (Set to 0 if none.)
 * \param nsid         NSID string. (Set to NULL if none.)
 *
 * \return New EDNS parameters structure or NULL if an error occured.
 */
knot_edns_params_t *knot_edns_new_params(uint16_t max_payload, uint8_t ver,
                                         uint16_t flags, uint16_t nsid_len,
                                         uint8_t *nsid);

/*!
 * \brief Properly frees the EDNS parameters structure. (With the NSID.)
 *
 * \param edns EDNS parameters structure to be freed.
 */
void knot_edns_free_params(knot_edns_params_t **edns);

/*----------------------------------------------------------------------------*/
/* EDNS OPT RR handling functions.                                            */
/*----------------------------------------------------------------------------*/
/*!
 * \brief Initializes given RRSet structure as an OPT RR with parameters taken
 *        from the given EDNS params.
 *
 * \param opt_rr    RRSet to initialize.
 * \param params    EDNS parameters structure to use.
 * \param add_nsid  Add NSID from the parameters to the OPT RR.
 * \param mm        Memory context to use (set to NULL if none available).
 *
 * \retval KNOT_EOK on success.
 * \retval KNOT_EINVAL when bad parameters are supplied.
 * \retval KNOT_ENOMEM if some allocation failed.
 */
int knot_edns_init_from_params(knot_rrset_t *opt_rr,
                               const knot_edns_params_t *params, bool add_nsid,
                               mm_ctx_t *mm);

/*!
 * \brief Returns the Max UDP payload value stored in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to get the value from.
 *
 * \return Max UDP payload in bytes.
 */
uint16_t knot_edns_get_payload(const knot_rrset_t *opt_rr);

/*!
 * \brief Sets the Max UDP payload field in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to set the value to.
 * \param payload UDP payload in bytes.
 */
void knot_edns_set_payload(knot_rrset_t *opt_rr, uint16_t payload);

/*!
 * \brief Returns the Extended RCODE stored in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to get the Extended RCODE from.
 *
 * \return Extended RCODE.
 */
uint8_t knot_edns_get_ext_rcode(const knot_rrset_t *opt_rr);

/*!
 * \brief Sets the Extended RCODE field in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to set the Extended RCODE to.
 * \param ext_rcode Extended RCODE to set.
 */
void knot_edns_set_ext_rcode(knot_rrset_t *opt_rr, uint8_t ext_rcode);

/*!
 * \brief Returns the EDNS version stored in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to get the EDNS version from.
 *
 * \return EDNS version.
 */
uint8_t knot_edns_get_version(const knot_rrset_t *opt_rr);

/*!
 * \brief Sets the EDNS version field in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to set the EDNS version to.
 * \param version EDNS version to set.
 */
void knot_edns_set_version(knot_rrset_t *opt_rr, uint8_t version);

/*!
 * \brief Returns the state of the DO bit in the OPT RR flags.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to get the DO bit from.
 *
 * \return <> 0 if the DO bit is set.
 * \return 0 if the DO bit is not set.
 */
bool knot_edns_do(const knot_rrset_t *opt_rr);

/*!
 * \brief Sets the DO bit in the OPT RR.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to set the DO bit in.
 */
void knot_edns_set_do(knot_rrset_t *opt_rr);

/*!
 * \brief Adds EDNS Option to the OPT RR.
 *
 * \param opt_rr  OPT RR structure to add the Option to.
 * \param code    Option code.
 * \param length  Option data length in bytes.
 * \param data    Option data.
 *
 * \retval KNOT_EOK
 * \retval KNOT_ENOMEM
 */
int knot_edns_add_option(knot_rrset_t *opt_rr, uint16_t code,
                         uint16_t length, const uint8_t *data, mm_ctx_t *mm);

/*!
 * \brief Checks if the OPT RR contains Option with the specified code.
 *
 * \param opt_rr OPT RR structure to check for the Option in.
 * \param code Option code to check for.
 *
 * \retval <> 0 if the OPT RR contains Option with Option code \a code.
 * \retval 0 otherwise.
 */
bool knot_edns_has_option(const knot_rrset_t *opt_rr, uint16_t code);

/*!
 * \brief Returns size of the OPT RR in wire format.
 *
 * \warning This function does not check the parameter, so ensure to check it
 *          before calling the function. It must not be NULL.
 * \note There is an assert() for debug checking of the parameter.
 *
 * \param opt_rr OPT RR to get the size of.
 *
 * \return Size of the OPT RR in bytes.
 */
size_t knot_edns_size(knot_rrset_t *opt_rr);

#endif /* _KNOT_EDNS_H_ */

/*! @} */

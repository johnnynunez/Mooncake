package p2pstore

import "errors"

// Errors
var ErrInvalidArgument = errors.New("error: invalid argument")
var ErrAddressOverlapped = errors.New("error: address overlapped")
var ErrPayloadOpened = errors.New("error: payload has been replicated")
var ErrPayloadNotOpened = errors.New("error: payload does not replicated")
var ErrPayloadNotFound = errors.New("error: payload not found in metadata")
var ErrTooManyRetries = errors.New("error: too many retries")
var ErrTransferEngine = errors.New("error: transfer engine core")

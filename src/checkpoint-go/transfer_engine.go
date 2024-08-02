package main

//#cgo LDFLAGS: -L../../build/src/transfer_engine -ltransfer_engine_shared
//#include "../transfer_engine/transfer_engine_c.h"
import "C"

import (
	"errors"
	"unsafe"
)

type BatchID int64

type TransferEngine struct {
	engine C.transfer_engine_t
}

func NewTransferEngine(metadata_uri string, local_server_name string, nic_priority_matrix string) (*TransferEngine, error) {
	engine := &TransferEngine{
		engine: C.createTransferEngine(C.CString(metadata_uri),
			C.CString(local_server_name),
			C.CString(nic_priority_matrix)),
	}
	return engine, nil
}

func (engine *TransferEngine) Close() error {
	C.destroyTransferEngine(engine.engine)
	return nil
}

func (engine *TransferEngine) registerLocalMemory(addr uintptr, length uint64, location string) error {
	ret := C.registerLocalMemory(engine.engine, unsafe.Pointer(addr), C.size_t(length), C.CString(location))
	if ret < 0 {
		return errors.New("transferEngine error")
	}
	return nil
}

func (engine *TransferEngine) unregisterLocalMemory(addr uintptr) error {
	ret := C.unregisterLocalMemory(engine.engine, unsafe.Pointer(addr))
	if ret < 0 {
		return errors.New("transferEngine error")
	}
	return nil
}

func (engine *TransferEngine) allocateBatchID(batchSize int) (BatchID, error) {
	ret := C.allocateBatchID(engine.engine, C.size_t(batchSize))
	if ret == C.UINT64_MAX {
		return BatchID(-1), errors.New("transferEngine error")
	}
	return BatchID(ret), nil
}

const (
	OPCODE_READ      = 0
	OPCODE_WRITE     = 1
	STATUS_WAITING   = 0
	STATUS_PENDING   = 1
	STATUS_INVALID   = 2
	STATUS_CANNELED  = 3
	STATUS_COMPLETED = 4
	STATUS_TIMEOUT   = 5
	STATUS_FAILED    = 6
)

type TransferRequest struct {
	Opcode       int
	Source       uint64
	TargetID     int64
	TargetOffset uint64
	Length       uint64
}

func (engine *TransferEngine) submitTransfer(batchID BatchID, requests []TransferRequest) error {
	requestSlice := make([]C.transfer_request_t, len(requests))
	for i, req := range requests {
		requestSlice[i] = C.transfer_request_t{
			opcode:        C.int(req.Opcode),
			source:        unsafe.Pointer(uintptr(req.Source)),
			target_id:     C.segment_id_t(req.TargetID),
			target_offset: C.uint64_t(req.TargetOffset),
			length:        C.uint64_t(req.Length),
		}
	}

	ret := C.submitTransfer(engine.engine, C.batch_id_t(batchID), &requestSlice[0], C.size_t(len(requests)))
	if ret < 0 {
		return errors.New("transferEngine error")
	}
	return nil
}

func (engine *TransferEngine) getTransferStatus(batchID BatchID, taskID int) (int, uint64, error) {
	var status C.transfer_status_t
	ret := C.getTransferStatus(engine.engine, C.batch_id_t(batchID), C.size_t(taskID), &status)
	if ret < 0 {
		return -1, 0, errors.New("transferEngine error")
	}
	return int(status.status), uint64(status.transferred_bytes), nil
}

func (engine *TransferEngine) freeBatchID(batchID BatchID) error {
	ret := C.freeBatchID(engine.engine, C.batch_id_t(batchID))
	if ret < 0 {
		return errors.New("transferEngine error")
	}
	return nil
}

func (engine *TransferEngine) getSegmentID(name string) (int64, error) {
	ret := C.getSegmentID(engine.engine, C.CString(name))
	if ret < 0 {
		return -1, errors.New("transferEngine error")
	}
	return int64(ret), nil
}

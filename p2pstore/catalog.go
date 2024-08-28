package p2pstore

import "sync"

type CatalogParams struct {
	IsGold       bool
	AddrList     []uintptr
	SizeList     []uint64
	MaxShardSize uint64
}

type Catalog struct {
	entries map[string]CatalogParams
	mu      sync.Mutex
}

func NewCatalog() *Catalog {
	catalog := &Catalog{
		entries: make(map[string]CatalogParams),
	}
	return catalog
}

func (catalog *Catalog) Contains(name string) bool {
	catalog.mu.Lock()
	defer catalog.mu.Unlock()
	_, exist := catalog.entries[name]
	return exist
}

func (catalog *Catalog) Get(name string) (CatalogParams, bool) {
	catalog.mu.Lock()
	defer catalog.mu.Unlock()
	params, exist := catalog.entries[name]
	return params, exist
}

func (catalog *Catalog) Add(name string, params CatalogParams) {
	catalog.mu.Lock()
	defer catalog.mu.Unlock()
	catalog.entries[name] = params
}

func (catalog *Catalog) Remove(name string) {
	catalog.mu.Lock()
	defer catalog.mu.Unlock()
	delete(catalog.entries, name)
}

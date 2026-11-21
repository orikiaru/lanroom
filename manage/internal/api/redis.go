package api

import (
	"context"
	"sync"
	"time"

	"github.com/redis/go-redis/v9"
)

type Store interface {
	Get(ctx context.Context, key string) (string, bool)
	Set(ctx context.Context, key, value string, ttl time.Duration) error
	Del(ctx context.Context, keys ...string) error
}

type memoryStore struct {
	mu sync.Mutex
	m  map[string]string
}

func newMemoryStore() *memoryStore {
	return &memoryStore{m: map[string]string{}}
}

func (s *memoryStore) Get(_ context.Context, key string) (string, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	value, ok := s.m[key]
	return value, ok
}

func (s *memoryStore) Set(_ context.Context, key, value string, _ time.Duration) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.m[key] = value
	return nil
}

func (s *memoryStore) Del(_ context.Context, keys ...string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, key := range keys {
		delete(s.m, key)
	}
	return nil
}

type redisStore struct {
	client *redis.Client
}

func newRedisStore(url string) (*redisStore, error) {
	opt, err := redis.ParseURL(url)
	if err != nil {
		return nil, err
	}
	return &redisStore{client: redis.NewClient(opt)}, nil
}

func (s *redisStore) Get(ctx context.Context, key string) (string, bool) {
	value, err := s.client.Get(ctx, key).Result()
	if err != nil {
		return "", false
	}
	return value, true
}

func (s *redisStore) Set(ctx context.Context, key, value string, ttl time.Duration) error {
	return s.client.Set(ctx, key, value, ttl).Err()
}

func (s *redisStore) Del(ctx context.Context, keys ...string) error {
	if len(keys) == 0 {
		return nil
	}
	return s.client.Del(ctx, keys...).Err()
}

func roomKey(name string) string { return "lanroom:room:" + name }
func liveKey(name string) string { return "lanroom:live:" + name }
func kickKey(name, mac string) string {
	return "lanroom:kick:" + name + ":" + mac
}

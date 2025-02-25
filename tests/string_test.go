/*
 * Copyright (c) 2023-present, OpenAtom Foundation, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

package pikiwidb_test

import (
	"context"
	"log"
	"strconv"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"github.com/redis/go-redis/v9"

	"github.com/OpenAtomFoundation/pikiwidb/tests/util"
)

var _ = Describe("String", Ordered, func() {
	var (
		ctx    = context.TODO()
		s      *util.Server
		client *redis.Client
	)

	s2s := map[string]string{
		"key_1": "value_1",
		"key_2": "value_2",
		"key_3": "value_3",
		"key_4": "value_4",
		"key_5": "value_5",
		"key_6": "value_6",
	}

	s2i := map[string]int{
		"ikey_1": 1, "ikey_2": 2, "ikey_3": 3,
		"ikey_4": 4, "ikey_5": 5, "ikey_6": 6,
	}

	// BeforeAll closures will run exactly once before any of the specs
	// within the Ordered container.
	BeforeAll(func() {
		config := util.GetConfPath(false, 0)

		s = util.StartServer(config, map[string]string{"port": strconv.Itoa(7777)}, true)
		Expect(s).NotTo(Equal(nil))
	})

	// AfterAll closures will run exactly once after the last spec has
	// finished running.
	AfterAll(func() {
		err := s.Close()
		if err != nil {
			log.Println("Close Server fail.", err.Error())
			return
		}
	})

	// When running each spec Ginkgo will first run the BeforeEach
	// closure and then the subject closure.Doing so ensures that
	// each spec has a pristine, correctly initialized, copy of the
	// shared variable.
	BeforeEach(func() {
		log.Println("before")
		client = s.NewClient()
	})

	// nodes that run after the spec's subject(It).
	AfterEach(func() {
		log.Println("after")
		err := client.Close()
		if err != nil {
			log.Println("Close client conn fail.", err.Error())
			return
		}
	})

	//TODO(dingxiaoshuai) Add more test cases.
	It("Cmd SET & GET", func() {
		log.Println("Cmd SET & GET Test Begin")
		{
			for k := range s2s {
				r, e := client.Get(ctx, k).Result()
				Expect(e).To(MatchError(redis.Nil))
				Expect(r).To(Equal(Nil))
			}

			for k := range s2i {
				r, e := client.Get(ctx, k).Result()
				Expect(e).To(MatchError(redis.Nil))
				Expect(r).To(Equal(Nil))
			}
		}

		{
			for k, v := range s2s {
				r, e := client.Set(ctx, k, v, 0).Result()
				Expect(e).NotTo(HaveOccurred())
				Expect(r).To(Equal(OK))
			}

			for k, v := range s2i {
				r, e := client.Set(ctx, k, v, 0).Result()
				Expect(e).NotTo(HaveOccurred())
				Expect(r).To(Equal(OK))
			}
		}

		{
			for k, v := range s2s {
				r, e := client.Get(ctx, k).Result()
				Expect(e).NotTo(HaveOccurred())
				Expect(r).To(Equal(v))
			}

			for k, v := range s2i {
				r, e := client.Get(ctx, k).Result()
				Expect(e).NotTo(HaveOccurred())
				Expect(r).To(Equal(strconv.Itoa(v)))
			}
		}

		{
			for k := range s2s {
				r, e := client.Del(ctx, k).Result()
				Expect(e).NotTo(HaveOccurred())
				Expect(r).To(Equal(int64(1)))
			}

			for k := range s2i {
				r, e := client.Del(ctx, k).Result()
				Expect(e).NotTo(HaveOccurred())
				Expect(r).To(Equal(int64(1)))
			}
		}
	})

	It("Cmd INCR", func() {
		log.Println("Cmd INCR Test Begin")
	})

	It("Append", func() {
		client.Del(ctx, DefaultKey) // delete this line if ping cmd valid 。
		n, err := client.Exists(ctx, DefaultKey).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(n).To(Equal(int64(0)))

		appendRes := client.Append(ctx, DefaultKey, "Hello")
		Expect(appendRes.Err()).NotTo(HaveOccurred())
		Expect(appendRes.Val()).To(Equal(int64(5)))

		appendRes = client.Append(ctx, DefaultKey, " World")
		Expect(appendRes.Err()).NotTo(HaveOccurred())
		Expect(appendRes.Val()).To(Equal(int64(11)))

		get := client.Get(ctx, DefaultKey)
		Expect(get.Err()).NotTo(HaveOccurred())
		Expect(get.Val()).To(Equal("Hello World"))

		r, e := client.Del(ctx, DefaultKey).Result()
		Expect(e).NotTo(HaveOccurred())
		Expect(r).To(Equal(int64(1)))
	})

	It("BitCount", func() {
		r, e := client.Set(ctx, DefaultKey, "foobar", 0).Result()
		Expect(e).NotTo(HaveOccurred())
		Expect(r).To(Equal(OK))

		rBitCount, eBitCount := client.BitCount(ctx, DefaultKey, nil).Result()
		Expect(eBitCount).NotTo(HaveOccurred())
		Expect(rBitCount).To(Equal(int64(26)))

		bitCount := client.BitCount(ctx, DefaultKey, &redis.BitCount{
			Start: 0,
			End:   0,
		})
		Expect(bitCount.Err()).NotTo(HaveOccurred())
		Expect(bitCount.Val()).To(Equal(int64(4)))

		bitCount = client.BitCount(ctx, DefaultKey, &redis.BitCount{
			Start: 1,
			End:   1,
		})
		Expect(bitCount.Err()).NotTo(HaveOccurred())
		Expect(bitCount.Val()).To(Equal(int64(6)))

		rDel, eDel := client.Del(ctx, DefaultKey).Result()
		Expect(eDel).NotTo(HaveOccurred())
		Expect(rDel).To(Equal(int64(1)))
	})

	It("should GetSet", func() {
		incr := client.Incr(ctx, DefaultKey)
		Expect(incr.Err()).NotTo(HaveOccurred())
		Expect(incr.Val()).To(Equal(int64(1)))

		getSet := client.GetSet(ctx, DefaultKey, "0")
		Expect(getSet.Err()).NotTo(HaveOccurred())
		Expect(getSet.Val()).To(Equal("1"))

		get := client.Get(ctx, DefaultKey)
		Expect(get.Err()).NotTo(HaveOccurred())
		Expect(get.Val()).To(Equal("0"))
	})

	It("MSet & MGet", func() {
		mSet := client.MSet(ctx, "key1", "hello1", "key2", "hello2")
		Expect(mSet.Err()).NotTo(HaveOccurred())
		Expect(mSet.Val()).To(Equal("OK"))

		mGet := client.MGet(ctx, "key1", "key2", "_")
		Expect(mGet.Err()).NotTo(HaveOccurred())
		Expect(mGet.Val()).To(Equal([]interface{}{"hello1", "hello2", nil}))

		// MSet struct
		type set struct {
			Set1 string                 `redis:"set1"`
			Set2 int16                  `redis:"set2"`
			Set3 time.Duration          `redis:"set3"`
			Set4 interface{}            `redis:"set4"`
			Set5 map[string]interface{} `redis:"-"`
		}
		mSet = client.MSet(ctx, &set{
			Set1: "val1",
			Set2: 1024,
			Set3: 2 * time.Millisecond,
			Set4: nil,
			Set5: map[string]interface{}{"k1": 1},
		})
		Expect(mSet.Err()).NotTo(HaveOccurred())
		Expect(mSet.Val()).To(Equal("OK"))

		mGet = client.MGet(ctx, "set1", "set2", "set3", "set4")
		Expect(mGet.Err()).NotTo(HaveOccurred())
		Expect(mGet.Val()).To(Equal([]interface{}{
			"val1",
			"1024",
			strconv.Itoa(int(2 * time.Millisecond.Nanoseconds())),
			"",
		}))
	})

	It("MSetnx & MGet", func() {
		mSetnx := client.MSetNX(ctx, "keynx1", "hello1", "keynx2", "hello2")
		Expect(mSetnx.Err()).NotTo(HaveOccurred())
		Expect(mSetnx.Val()).To(Equal(true))

		mGet := client.MGet(ctx, "keynx1", "keynx2", "_")
		Expect(mGet.Err()).NotTo(HaveOccurred())
		Expect(mGet.Val()).To(Equal([]interface{}{"hello1", "hello2", nil}))

		mSetnx = client.MSetNX(ctx, "keynx3", "hello3", "keynx2", "hello22")
		Expect(mSetnx.Err()).NotTo(HaveOccurred())
		Expect(mSetnx.Val()).To(Equal(false))

		mGet = client.MGet(ctx, "keynx2", "keynx3")
		Expect(mGet.Err()).NotTo(HaveOccurred())
		Expect(mGet.Val()).To(Equal([]interface{}{"hello2", nil}))

		mSetnx = client.MSetNX(ctx, "keynx1")
		Expect(mSetnx.Err()).To(HaveOccurred())
		Expect(mSetnx.Val()).To(Equal(false))
	})

})

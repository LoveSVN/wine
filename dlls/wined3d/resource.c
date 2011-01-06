/*
 * IWineD3DResource Implementation
 *
 * Copyright 2002-2004 Jason Edmeades
 * Copyright 2003-2004 Raphael Junqueira
 * Copyright 2004 Christian Costa
 * Copyright 2005 Oliver Stieber
 * Copyright 2009-2010 Henri Verbeet for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wined3d_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d);

struct private_data
{
    struct list entry;

    GUID tag;
    DWORD flags; /* DDSPD_* */

    union
    {
        void *data;
        IUnknown *object;
    } ptr;

    DWORD size;
};

HRESULT resource_init(struct IWineD3DResourceImpl *resource, WINED3DRESOURCETYPE resource_type,
        IWineD3DDeviceImpl *device, UINT size, DWORD usage, const struct wined3d_format *format,
        WINED3DPOOL pool, void *parent, const struct wined3d_parent_ops *parent_ops)
{
    struct IWineD3DResourceClass *r = &resource->resource;

    r->device = device;
    r->resourceType = resource_type;
    r->ref = 1;
    r->pool = pool;
    r->format = format;
    r->usage = usage;
    r->size = size;
    r->priority = 0;
    r->parent = parent;
    r->parent_ops = parent_ops;
    list_init(&r->privateData);

    if (size)
    {
        r->heapMemory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + RESOURCE_ALIGNMENT);
        if (!r->heapMemory)
        {
            ERR("Out of memory!\n");
            return WINED3DERR_OUTOFVIDEOMEMORY;
        }
    }
    else
    {
        r->heapMemory = NULL;
    }
    r->allocatedMemory = (BYTE *)(((ULONG_PTR)r->heapMemory + (RESOURCE_ALIGNMENT - 1)) & ~(RESOURCE_ALIGNMENT - 1));

    /* Check that we have enough video ram left */
    if (pool == WINED3DPOOL_DEFAULT)
    {
        if (size > IWineD3DDevice_GetAvailableTextureMem((IWineD3DDevice *)device))
        {
            ERR("Out of adapter memory\n");
            HeapFree(GetProcessHeap(), 0, r->heapMemory);
            return WINED3DERR_OUTOFVIDEOMEMORY;
        }
        WineD3DAdapterChangeGLRam(device, size);
    }

    device_resource_add(device, (IWineD3DResource *)resource);

    return WINED3D_OK;
}

void resource_cleanup(struct IWineD3DResourceImpl *resource)
{
    struct private_data *data;
    struct list *e1, *e2;
    HRESULT hr;

    TRACE("Cleaning up resource %p.\n", resource);

    if (resource->resource.pool == WINED3DPOOL_DEFAULT)
    {
        TRACE("Decrementing device memory pool by %u.\n", resource->resource.size);
        WineD3DAdapterChangeGLRam(resource->resource.device, -resource->resource.size);
    }

    LIST_FOR_EACH_SAFE(e1, e2, &resource->resource.privateData)
    {
        data = LIST_ENTRY(e1, struct private_data, entry);
        hr = resource_free_private_data((IWineD3DResource *)resource, &data->tag);
        if (FAILED(hr))
            ERR("Failed to free private data when destroying resource %p, hr = %#x.\n", resource, hr);
    }

    HeapFree(GetProcessHeap(), 0, resource->resource.heapMemory);
    resource->resource.allocatedMemory = 0;
    resource->resource.heapMemory = 0;

    if (resource->resource.device)
        device_resource_released(resource->resource.device, (IWineD3DResource *)resource);
}

void resource_unload(IWineD3DResourceImpl *resource)
{
    context_resource_unloaded(resource->resource.device, (IWineD3DResource *)resource,
            resource->resource.resourceType);
}

static struct private_data *resource_find_private_data(IWineD3DResourceImpl *This, REFGUID tag)
{
    struct private_data *data;
    struct list *entry;

    TRACE("Searching for private data %s\n", debugstr_guid(tag));
    LIST_FOR_EACH(entry, &This->resource.privateData)
    {
        data = LIST_ENTRY(entry, struct private_data, entry);
        if (IsEqualGUID(&data->tag, tag)) {
            TRACE("Found %p\n", data);
            return data;
        }
    }
    TRACE("Not found\n");
    return NULL;
}

HRESULT resource_set_private_data(IWineD3DResource *iface, REFGUID refguid,
        const void *pData, DWORD SizeOfData, DWORD flags)
{
    IWineD3DResourceImpl *This = (IWineD3DResourceImpl *)iface;
    struct private_data *data;

    TRACE("iface %p, riid %s, data %p, data_size %u, flags %#x.\n",
            iface, debugstr_guid(refguid), pData, SizeOfData, flags);

    resource_free_private_data(iface, refguid);

    data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*data));
    if (!data) return E_OUTOFMEMORY;

    data->tag = *refguid;
    data->flags = flags;

    if (flags & WINED3DSPD_IUNKNOWN)
    {
        if (SizeOfData != sizeof(IUnknown *))
        {
            WARN("IUnknown data with size %d, returning WINED3DERR_INVALIDCALL\n", SizeOfData);
            HeapFree(GetProcessHeap(), 0, data);
            return WINED3DERR_INVALIDCALL;
        }
        data->ptr.object = (LPUNKNOWN)pData;
        data->size = sizeof(LPUNKNOWN);
        IUnknown_AddRef(data->ptr.object);
    }
    else
    {
        data->ptr.data = HeapAlloc(GetProcessHeap(), 0, SizeOfData);
        if (!data->ptr.data)
        {
            HeapFree(GetProcessHeap(), 0, data);
            return E_OUTOFMEMORY;
        }
        data->size = SizeOfData;
        memcpy(data->ptr.data, pData, SizeOfData);
    }
    list_add_tail(&This->resource.privateData, &data->entry);

    return WINED3D_OK;
}

HRESULT resource_get_private_data(IWineD3DResource *iface, REFGUID refguid, void *pData, DWORD *pSizeOfData)
{
    IWineD3DResourceImpl *This = (IWineD3DResourceImpl *)iface;
    struct private_data *data;

    TRACE("(%p) : %p %p %p\n", This, refguid, pData, pSizeOfData);
    data = resource_find_private_data(This, refguid);
    if (!data) return WINED3DERR_NOTFOUND;

    if (*pSizeOfData < data->size) {
        *pSizeOfData = data->size;
        return WINED3DERR_MOREDATA;
    }

    if (data->flags & WINED3DSPD_IUNKNOWN) {
        *(LPUNKNOWN *)pData = data->ptr.object;
        if (((IWineD3DImpl *)This->resource.device->wined3d)->dxVersion != 7)
        {
            /* D3D8 and D3D9 addref the private data, DDraw does not. This can't be handled in
             * ddraw because it doesn't know if the pointer returned is an IUnknown * or just a
             * Blob
             */
            IUnknown_AddRef(data->ptr.object);
        }
    }
    else {
        memcpy(pData, data->ptr.data, data->size);
    }

    return WINED3D_OK;
}
HRESULT resource_free_private_data(IWineD3DResource *iface, REFGUID refguid)
{
    IWineD3DResourceImpl *This = (IWineD3DResourceImpl *)iface;
    struct private_data *data;

    TRACE("(%p) : %s\n", This, debugstr_guid(refguid));
    data = resource_find_private_data(This, refguid);
    if (!data) return WINED3DERR_NOTFOUND;

    if (data->flags & WINED3DSPD_IUNKNOWN)
    {
        if (data->ptr.object)
            IUnknown_Release(data->ptr.object);
    }
    else
    {
        HeapFree(GetProcessHeap(), 0, data->ptr.data);
    }
    list_remove(&data->entry);

    HeapFree(GetProcessHeap(), 0, data);

    return WINED3D_OK;
}

DWORD resource_set_priority(IWineD3DResource *iface, DWORD PriorityNew)
{
    IWineD3DResourceImpl *This = (IWineD3DResourceImpl *)iface;
    DWORD PriorityOld = This->resource.priority;
    This->resource.priority = PriorityNew;
    TRACE("(%p) : new priority %d, returning old priority %d\n", This, PriorityNew, PriorityOld );
    return PriorityOld;
}

DWORD resource_get_priority(IWineD3DResource *iface)
{
    IWineD3DResourceImpl *This = (IWineD3DResourceImpl *)iface;
    TRACE("(%p) : returning %d\n", This, This->resource.priority );
    return This->resource.priority;
}

WINED3DRESOURCETYPE resource_get_type(IWineD3DResource *iface)
{
    IWineD3DResourceImpl *This = (IWineD3DResourceImpl *)iface;
    TRACE("(%p) : returning %d\n", This, This->resource.resourceType);
    return This->resource.resourceType;
}

"""Bound multipart spooling, including requests without Content-Length."""
import asyncio
from fastapi.responses import JSONResponse
from demos.server.v1_product.archive import ArchiveError


class UploadLimit:
    def __init__(self, app, limit=7 * 1024 * 1024):
        self.app = app
        self.limit = limit
        self.transfers = asyncio.Semaphore(2)

    async def __call__(self, scope, receive, send):
        if scope['type'] != 'http' or scope['method'] not in {'POST', 'PUT'}:
            return await self.app(scope, receive, send)
        headers = dict(scope.get('headers', []))
        try:
            length = int(headers.get(b'content-length', b'0'))
        except ValueError:
            length = -1
        if length < 0 or length > self.limit:
            return await JSONResponse({'error': 'request too large or invalid length'}, status_code=413)(scope, receive, send)
        count = 0

        async def bounded_receive():
            nonlocal count
            message = await receive()
            count += len(message.get('body', b''))
            if count > self.limit:
                raise ArchiveError(413, 'request too large')
            return message

        is_media = scope['path'].startswith('/v1/messages') or scope['path'] in {'/v1/admin/messages', '/v1/admin/welcome'}
        if is_media:
            async with self.transfers:
                await self.app(scope, bounded_receive, send)
        else:
            await self.app(scope, bounded_receive, send)

import xmlrpclib

proxy = None

def ProxyInit(machine, username, password):
	global proxy
	if machine and username and password:
		url = "http://" + username + ":" + password + "@" + machine + "/xmlrpc/WebRequestServer[0]"
		result = "http://" + username + "@" + machine + "/xmlrpc/WebRequestServer[0]"
	elif machine and username:
		url = "http://" + username + "@" + machine + "/xmlrpc/WebRequestServer[0]"
		result = url
	elif machine:
		url = "http://" + machine + "/xmlrpc/WebRequestServer[0]"
		result = url
	else:
		url = "http://localhost/xmlrpc/WebRequestServer[0]"
		result = url
	proxy = xmlrpclib.ServerProxy(url)
	return result

def Version():
	global proxy
	return proxy.version()

def SearchInit(includeSaved, includePublished, search):
	global proxy
	return proxy.UGCExport_SearchInit(includeSaved, includePublished, search)

def SearchNext(searchKey):
	global proxy
	return proxy.UGCExport_SearchNext(searchKey)

def GetUGCPatchInfo():
	global proxy
	return proxy.UGCExport_GetUGCPatchInfo()

def GetUGCProjectContainer(id):
	global proxy
	return proxy.UGCExport_GetUGCProjectContainer(id)

def GetUGCProjectSeriesContainer(id):
	global proxy
	return proxy.UGCExport_GetUGCProjectSeriesContainer(id)

def DeleteAllUGC(comment):
	global proxy
	return proxy.UGCImport_DeleteAllUGC(comment)

def ImportUGCProjectContainerAndData(ugcProject, ugcProjectDataPublished, ugcProjectDataSaved, previousShard, comment, forceDelete):
	global proxy
	return proxy.UGCImport_ImportUGCProjectContainerAndData(ugcProject, ugcProjectDataPublished, ugcProjectDataSaved, previousShard, comment, forceDelete)

def ImportUGCProjectSeriesContainer(ugcProjectSeries, previousShard, comment, forceDelete):
	global proxy
	return proxy.UGCImport_ImportUGCProjectSeriesContainer(ugcProjectSeries, previousShard, comment, forceDelete)

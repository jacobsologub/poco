//
// WebApplication.cpp
//
// $Id: //poco/Main/WebWidgets/src/WebApplication.cpp#5 $
//
// Library: WebWidgets
// Package: Core
// Module:  WebApplication
//
// Copyright (c) 2008, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/WebWidgets/WebApplication.h"
#include "Poco/WebWidgets/RequestProcessor.h"
#include "Poco/WebWidgets/SubmitButtonCell.h"
#include "Poco/WebWidgets/SubmitButton.h"
#include "Poco/WebWidgets/WebWidgetsException.h"
#include "Poco/Net/HTMLForm.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/NumberFormatter.h"
#include "Poco/NumberParser.h"


namespace Poco {
namespace WebWidgets {


Poco::ThreadLocal<WebApplication*> WebApplication::_pInstance;
Poco::ThreadLocal<std::string> WebApplication::_clientMachine;


WebApplication::WebApplication(const Poco::URI& uri,ResourceManager::Ptr pRM):
	_pResource(pRM),
	_pLookAndFeel(),
	_pCurrentPage(),
	_uri(uri)
{
	poco_check_ptr (pRM);
	*_pInstance = this;
	*_clientMachine = "";
}


WebApplication::~WebApplication()
{
}


void WebApplication::setLookAndFeel(LookAndFeel::Ptr pLookAndFeel)
{
	_pLookAndFeel = pLookAndFeel;
}


void WebApplication::setCurrentPage(Page::Ptr pPage)
{
	_pCurrentPage = pPage;
	_formMap.clear();
	_ajaxProcessorMap.clear();
	_submitButtons.clear();
	while (!_forms.empty())
		_forms.pop();
}


void WebApplication::attachToThread(Poco::Net::HTTPServerRequest& request)
{
	*_pInstance = this;
	*_clientMachine = request.getHost();
}


WebApplication& WebApplication::instance()
{
	WebApplication* pWebApp = *_pInstance;
	poco_check_ptr (pWebApp);
	return *pWebApp;
}


std::string WebApplication::clientHostName()
{
	return *_clientMachine;
}


void WebApplication::beginForm(const Form& form)
{
	if (!_forms.empty())
		throw WebWidgetsException("nested forms not allowed");
	_forms.push(form.id());
	_formMap.insert(std::make_pair(form.id(), RequestProcessorMap()));
}
		

void WebApplication::registerFormProcessor(const std::string& fieldName, RequestProcessor* pProc)
{
	// per default we register everyting that has a name as form processor
	if (!_forms.empty())
	{
		FormMap::iterator itForm = _formMap.find(_forms.top());
		poco_assert (itForm != _formMap.end());
		std::pair<RequestProcessorMap::iterator, bool> res = itForm->second.insert(std::make_pair(fieldName, pProc));
		if (!res.second)
			res.first->second = pProc;
	}
}


RequestProcessor* WebApplication::getFormProcessor(Renderable::ID formId, const std::string& fieldName)
{
	FormMap::iterator itForm = _formMap.find(formId);
	if (itForm == _formMap.end())
		return 0;
		
	RequestProcessorMap::iterator it = itForm->second.find(fieldName);
	if (it == itForm->second.end())
		return 0;
	return it->second;
}


void WebApplication::endForm(const Form& form)
{
	poco_assert_dbg (_forms.size() == 1);
	poco_assert_dbg (_forms.top() == form.id());
	_forms.pop();
}


void WebApplication::registerAjaxProcessor(const std::string& id, RequestProcessor* pProc)
{
	SubmitButtonCell* pCell = dynamic_cast<SubmitButtonCell*>(pProc);
	if (pCell)
	{
		if (_forms.empty())
			throw Poco::WebWidgets::WebWidgetsException("submitButton without outer Form detected");
		std::pair<SubmitButtons::iterator, bool> res = _submitButtons.insert(std::make_pair(_forms.top(), pCell));
		if (!res.second)
			res.first->second = pCell;
	}	
	std::pair<RequestProcessorMap::iterator, bool> res = _ajaxProcessorMap.insert(std::make_pair(id, pProc));
	if (!res.second)
		res.first->second = pProc;
}


RequestProcessor* WebApplication::getAjaxProcessor(const std::string& id)
{
	RequestProcessorMap::iterator it = _ajaxProcessorMap.find(id);
	if (it == _ajaxProcessorMap.end())
		return 0;
	return it->second;
}


void WebApplication::handleForm(const Poco::Net::HTMLForm& form)
{
	Renderable::ID formID = Poco::NumberParser::parse(form.get(Form::FORM_ID));
	FormMap::iterator itForm = _formMap.find(formID);
	if (itForm == _formMap.end())
		throw Poco::NotFoundException("unknown form id");
		
	Poco::Net::NameValueCollection::ConstIterator it = form.begin();	
	RequestProcessorMap& processors = itForm->second;
	for (;it != form.end(); ++it)
	{
		const std::string& key = it->first;
		RequestProcessorMap::iterator itR = processors.find(key);
		if (itR != processors.end())
		{
			itR->second->handleForm(key, it->second);
			processors.erase(itR);
		}
	}
	//those that are not included are either deselected or empty
	RequestProcessorMap::iterator itR = processors.begin();
	std::string empty;
	for (; itR != processors.end(); ++itR)
	{
		itR->second->handleForm(itR->first, empty);
	}
	processors.clear();
}


void WebApplication::notifySubmitButton(Renderable::ID id)
{
	SubmitButtons::iterator it = _submitButtons.find(id);
	if (it == _submitButtons.end())
		throw WebWidgetsException("failed to find submitButton with id " + Poco::NumberFormatter::format(id));
		
	View* pOwner = it->second->getOwner();
	poco_assert (pOwner);
	SubmitButton* pSubmit = dynamic_cast<SubmitButton*>(pOwner);
	if (pSubmit)
	{
		Button::ButtonEvent clickedEvent(pSubmit);
		pSubmit->buttonClicked.notify(pSubmit, clickedEvent);
	}
}


} } // namespace Poco::WebWidgets

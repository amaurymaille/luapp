local a = {}
a["a"] = 12
print (a["a"])
print (a.a)
local b = a
b.b = 12
print (a.b)
a[a] = 12
print (a[a])
print (a[{}])
a.c = function(x) 
    print ("Inside the closure: " .. x.a)
    x.a = x.a + 1
end
for i = 1, 10 do
    a:c()
end
